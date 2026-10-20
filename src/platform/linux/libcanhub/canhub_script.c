#include "canhub.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/txn_script/script_compiler.h"
#include "client/txn_script/script_generator.h"
#include "client/txn_script/script_parser.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/libcanhub/canhub_internal.h"
#include "protocol/frame_message.h"

#define SCRIPT_TEXT_SIZE_MAX (1024 * 1024)
#define SCRIPT_PATH_MAX 1024
#define SCRIPT_MESSAGE_SIZE 512
#define STEP_BATCH 64
#define SLICE_MS 200
#define PROGRESS_PERIOD_US 200000
#define GRANT_TIMEOUT_MS 5000
#define PATH_SEPARATOR '/'
#define WINDOWS_PATH_SEPARATOR '\\'

typedef struct {
    Script script;
    ScriptProgram program;
    ScriptGenerator generator;
    char *text;
    size_t text_size;
    char directory[SCRIPT_PATH_MAX];
    char open_path[SCRIPT_PATH_MAX];
    FILE *open_file;
} ScriptRun;

typedef struct {
    CanHubSession *session;
    CanHubScriptProgress on_progress;
    void *context;
    uint32_t total_steps;
    uint64_t reported_at_us;
} Progress;

static bool loadScript(ScriptRun *run, CanHubSession *session, const char *script_path);
static void keepDirectory(ScriptRun *run, const char *script_path);
static bool readScriptFile(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size);
static bool resolvePath(const ScriptRun *run, const char *path, char *resolved, size_t resolved_size);
static int32_t compileScript(ScriptRun *run, CanHubSession *session, const char *script_path, CanHubScriptOutcome *outcome);
static int32_t countSteps(ScriptRun *run, CanHubSession *session, const char *script_path, CanHubScriptOutcome *outcome);
static int32_t reportScriptError(CanHubSession *session, const char *script_path, const ScriptError *error, CanHubScriptOutcome *outcome);
static int32_t beginTransaction(ScriptRun *run, CanHubSession *session);
static int32_t defineTemplates(ScriptRun *run, CanHubSession *session);
static int32_t streamSteps(ScriptRun *run, Progress *progress, const char *script_path, CanHubScriptOutcome *outcome);
static int32_t sendBatch(Progress *progress, const CanHubTxnStep *steps, uint32_t count);
static int32_t endTransaction(Progress *progress);
static int32_t waitOutcome(Progress *progress, CanHubScriptOutcome *outcome);
static bool keepGoing(Progress *progress);
static void cancel(const Progress *progress);
static void toPublicTemplate(const TxnTemplateMessage *message, CanHubTxnTemplate *definition);
static void toPublicStep(const TxnStep *step, CanHubTxnStep *public_step);
static void fillRoster(const ScriptRun *run, CanHubScriptOutcome *outcome);
static void releaseRun(ScriptRun *run);

/* ---------- public ---------- */

int32_t canhub_txn_run_script(
    CanHubSession *session,
    const char *script_path,
    CanHubScriptOutcome *outcome,
    CanHubScriptProgress on_progress,
    void *context
)
{
    Progress progress = { session, on_progress, context, 0, 0 };
    ScriptRun *run;
    int32_t result;

    if (script_path == NULL || outcome == NULL || outcome->struct_size != sizeof(*outcome)) {
        CanHubSession_SetError(session, "script path and outcome with struct_size required");
        return CANHUB_ERR_ARGUMENT;
    }
    memset(outcome, 0, sizeof(*outcome));
    outcome->struct_size = sizeof(*outcome);
    outcome->result.struct_size = sizeof(outcome->result);

    run = calloc(1, sizeof(*run));
    if (run == NULL) {
        CanHubSession_SetError(session, "out of memory for the script");
        return CANHUB_ERR_STATE;
    }

    result = loadScript(run, session, script_path) ? compileScript(run, session, script_path, outcome) : CANHUB_ERR_ARGUMENT;
    if (result == CANHUB_OK) {
        result = countSteps(run, session, script_path, outcome);
    }
    progress.total_steps = outcome->total_steps;
    if (result == CANHUB_OK) {
        result = beginTransaction(run, session);
    }
    if (result == CANHUB_OK) {
        result = defineTemplates(run, session);
    }
    if (result == CANHUB_OK) {
        result = streamSteps(run, &progress, script_path, outcome);
    }
    if (result == CANHUB_OK) {
        result = endTransaction(&progress);
    }
    if (result == CANHUB_OK || result == CANHUB_ERR_TXN_ABORTED) {
        result = waitOutcome(&progress, outcome);
    }

    releaseRun(run);
    return result;
}

/* ---------- private ---------- */

static bool loadScript(ScriptRun *run, CanHubSession *session, const char *script_path)
{
    FILE *file = fopen(script_path, "rb");
    int64_t size;

    if (file == NULL) {
        CanHubSession_SetError(session, "cannot open the script");
        return false;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0 || size > SCRIPT_TEXT_SIZE_MAX) {
        fclose(file);
        CanHubSession_SetError(session, "the script is larger than 1 MiB");
        return false;
    }

    run->text = malloc((size_t)size + 1);
    if (run->text == NULL || fread(run->text, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        CanHubSession_SetError(session, "cannot read the script");
        return false;
    }
    fclose(file);

    run->text_size = (size_t)size;
    keepDirectory(run, script_path);
    return true;
}

static void keepDirectory(ScriptRun *run, const char *script_path)
{
    const char *slash = strrchr(script_path, PATH_SEPARATOR);
    const char *backslash = strrchr(script_path, WINDOWS_PATH_SEPARATOR);
    uint32_t length;

    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
    if (slash == NULL) {
        run->directory[0] = '\0';
        return;
    }

    length = (uint32_t)(slash - script_path) + 1;
    if (length >= sizeof(run->directory)) {
        length = sizeof(run->directory) - 1;
    }
    memcpy(run->directory, script_path, length);
    run->directory[length] = '\0';
}

static bool readScriptFile(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size)
{
    ScriptRun *run = context;
    char resolved[SCRIPT_PATH_MAX];

    *read_size = 0;
    if (!resolvePath(run, path, resolved, sizeof(resolved))) {
        return false;
    }
    if (run->open_file == NULL || strcmp(run->open_path, resolved) != 0) {
        if (run->open_file != NULL) {
            fclose(run->open_file);
        }
        run->open_file = fopen(resolved, "rb");
        snprintf(run->open_path, sizeof(run->open_path), "%s", resolved);
    }
    if (run->open_file == NULL || fseek(run->open_file, offset, SEEK_SET) != 0) {
        return false;
    }

    *read_size = (uint32_t)fread(buffer, 1, size, run->open_file);
    return !ferror(run->open_file);
}

static bool resolvePath(const ScriptRun *run, const char *path, char *resolved, size_t resolved_size)
{
    int32_t written;

    if (path[0] == PATH_SEPARATOR || path[0] == WINDOWS_PATH_SEPARATOR || (path[0] != '\0' && path[1] == ':')) {
        written = snprintf(resolved, resolved_size, "%s", path);
    } else {
        written = snprintf(resolved, resolved_size, "%s%s", run->directory, path);
    }
    return written >= 0 && (size_t)written < resolved_size;
}

static int32_t compileScript(ScriptRun *run, CanHubSession *session, const char *script_path, CanHubScriptOutcome *outcome)
{
    ScriptError error;

    memset(&error, 0, sizeof(error));
    if (!ScriptParser_Parse(&run->script, run->text, run->text_size, &error) ||
        !ScriptCompiler_Compile(&run->program, &run->script, &error)) {
        return reportScriptError(session, script_path, &error, outcome);
    }

    fillRoster(run, outcome);
    return CANHUB_OK;
}

static int32_t countSteps(ScriptRun *run, CanHubSession *session, const char *script_path, CanHubScriptOutcome *outcome)
{
    ScriptFiles files = { run, readScriptFile };
    ScriptError error;
    TxnStep step;
    uint8_t generated;

    memset(&error, 0, sizeof(error));
    ScriptGenerator_Start(&run->generator, &run->program, &files);
    while ((generated = ScriptGenerator_Next(&run->generator, &step, &error)) == kSCRIPT_GENERATED_STEP) {
        outcome->total_steps++;
    }
    if (generated == kSCRIPT_GENERATED_ERROR) {
        return reportScriptError(session, script_path, &error, outcome);
    }
    if (outcome->total_steps == 0) {
        CanHubSession_SetError(session, "the script has no step");
        return CANHUB_ERR_ARGUMENT;
    }
    return CANHUB_OK;
}

static int32_t reportScriptError(CanHubSession *session, const char *script_path, const ScriptError *error, CanHubScriptOutcome *outcome)
{
    char message[SCRIPT_MESSAGE_SIZE];

    outcome->error_line = error->line_number;
    snprintf(message, sizeof(message), "%s:%" PRIu16 ": %s", script_path, error->line_number, error->message);
    CanHubSession_SetError(session, message);
    return CANHUB_ERR_ARGUMENT;
}

static int32_t beginTransaction(ScriptRun *run, CanHubSession *session)
{
    const TxnBeginMessage *wire = &run->program.begin;
    CanHubTxnBegin begin;

    memset(&begin, 0, sizeof(begin));
    begin.struct_size = sizeof(begin);
    begin.flags = wire->flags;
    begin.origin_source = wire->origin.source;
    begin.origin_shift = wire->origin.shift;
    begin.origin_payload_offset = wire->origin.payload_offset;
    begin.origin_mask = wire->origin.mask;
    begin.roster_count = wire->roster_count;
    memcpy(begin.roster, wire->roster, sizeof(begin.roster));
    return canhub_txn_begin(session, &begin, GRANT_TIMEOUT_MS);
}

static int32_t defineTemplates(ScriptRun *run, CanHubSession *session)
{
    CanHubTxnTemplate definition;
    int32_t result;
    uint8_t i;

    for(i=0; i<run->program.variant_count; i++) {
        toPublicTemplate(&run->program.variants[i], &definition);
        result = canhub_txn_template(session, &definition, GRANT_TIMEOUT_MS);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return CANHUB_OK;
}

static int32_t streamSteps(ScriptRun *run, Progress *progress, const char *script_path, CanHubScriptOutcome *outcome)
{
    ScriptFiles files = { run, readScriptFile };
    CanHubTxnStep batch[STEP_BATCH];
    ScriptError error;
    TxnStep step;
    uint32_t count = 0;
    uint8_t generated;
    int32_t result;

    memset(&error, 0, sizeof(error));
    ScriptGenerator_Start(&run->generator, &run->program, &files);
    while ((generated = ScriptGenerator_Next(&run->generator, &step, &error)) == kSCRIPT_GENERATED_STEP) {
        toPublicStep(&step, &batch[count++]);
        if (count < STEP_BATCH) {
            continue;
        }
        result = sendBatch(progress, batch, count);
        if (result != CANHUB_OK) {
            return result;
        }
        count = 0;
    }
    if (generated == kSCRIPT_GENERATED_ERROR) {
        cancel(progress);
        waitOutcome(progress, outcome);
        return reportScriptError(progress->session, script_path, &error, outcome);
    }
    return count > 0 ? sendBatch(progress, batch, count) : CANHUB_OK;
}

static int32_t sendBatch(Progress *progress, const CanHubTxnStep *steps, uint32_t count)
{
    uint32_t taken = 0;
    int32_t result;

    while (taken < count) {
        result = canhub_txn_send(progress->session, steps + taken, count - taken, SLICE_MS);
        if (result < 0) {
            return result;
        }
        taken += (uint32_t)result;
        if (!keepGoing(progress)) {
            cancel(progress);
            return CANHUB_ERR_TXN_ABORTED;
        }
    }
    return CANHUB_OK;
}

static int32_t endTransaction(Progress *progress)
{
    int32_t result;

    while ((result = canhub_txn_end(progress->session, SLICE_MS)) == CANHUB_ERR_TIMEOUT) {
        if (!keepGoing(progress)) {
            cancel(progress);
            return CANHUB_ERR_TXN_ABORTED;
        }
    }
    return result;
}

static int32_t waitOutcome(Progress *progress, CanHubScriptOutcome *outcome)
{
    int32_t result;
    bool cancelled = false;

    while ((result = canhub_txn_wait(progress->session, &outcome->result, SLICE_MS)) == CANHUB_ERR_TIMEOUT) {
        if (!cancelled && !keepGoing(progress)) {
            cancel(progress);
            cancelled = true;
        }
    }
    return result;
}

static bool keepGoing(Progress *progress)
{
    CanHubTxnProgress snapshot;
    uint64_t now_us = Clock_MonotonicUs();

    if (progress->on_progress == NULL || now_us - progress->reported_at_us < PROGRESS_PERIOD_US) {
        return true;
    }
    progress->reported_at_us = now_us;
    snapshot.struct_size = sizeof(snapshot);
    canhub_txn_progress(progress->session, &snapshot);
    return progress->on_progress(progress->context, &snapshot, progress->total_steps) != 0;
}

static void cancel(const Progress *progress)
{
    canhub_txn_abort(progress->session);
}

static void toPublicTemplate(const TxnTemplateMessage *message, CanHubTxnTemplate *definition)
{
    uint8_t i;

    memset(definition, 0, sizeof(*definition));
    definition->struct_size = sizeof(*definition);
    definition->template_id = message->template_id;
    definition->advance_mode = message->advance_mode;
    definition->flags = message->flags;
    definition->count_k = message->count_k;
    definition->patterns_present = message->patterns_present;
    definition->timeout_ms = message->timeout_ms;
    definition->gap_us = message->gap_us;
    for(i=0; i<CANHUB_TXN_PATTERNS; i++) {
        definition->patterns[i].can_id = message->patterns[i].can_id;
        definition->patterns[i].can_mask = message->patterns[i].can_mask;
        definition->patterns[i].payload_mask = message->patterns[i].payload_mask;
        definition->patterns[i].payload_value = message->patterns[i].payload_value;
    }
}

static void toPublicStep(const TxnStep *step, CanHubTxnStep *public_step)
{
    memset(public_step, 0, sizeof(*public_step));
    public_step->can_id = step->can_id;
    public_step->template_id = step->template_id;
    public_step->flags = step->flags;
    public_step->frame_flags = step->frame_flags;
    public_step->length = step->payload_length;
    public_step->expect_value = step->expect_value;
    memcpy(public_step->payload, step->payload, step->payload_length);
}

static void fillRoster(const ScriptRun *run, CanHubScriptOutcome *outcome)
{
    uint8_t length = run->program.roster_variable.length;

    outcome->roster_count = run->program.begin.roster_count;
    memcpy(outcome->roster, run->program.begin.roster, sizeof(outcome->roster));
    if (length == 0) {
        return;
    }
    if (length >= sizeof(outcome->roster_name)) {
        length = sizeof(outcome->roster_name) - 1;
    }
    memcpy(outcome->roster_name, run->program.roster_variable.text, length);
}

static void releaseRun(ScriptRun *run)
{
    if (run->open_file != NULL) {
        fclose(run->open_file);
    }
    free(run->text);
    free(run);
}
