#include "canhub.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/transaction_builder.h"
#include "client/txn_script/script_compiler.h"
#include "client/txn_script/script_generator.h"
#include "client/txn_script/script_parser.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/libcanhub/canhub_internal.h"

#define SCRIPT_TEXT_SIZE_MAX (1024 * 1024)
#define SCRIPT_PATH_MAX 1024
#define SCRIPT_MESSAGE_SIZE 512
#define SCRIPT_DETAIL_SIZE 256
#define PROGRESS_PERIOD_US 200000
#define WAIT_SLICE_MS 200
#define PATH_SEPARATOR '/'
#define WINDOWS_PATH_SEPARATOR '\\'
#define TEXT_LABEL "script"

typedef struct {
    Script script;
    ScriptProgram program;
    ScriptGenerator generator;
    TransactionBuilder builder;
    TransactionBuild build;
    CanHubTxnKeepalive keepalive;
    const char *text;
    char *owned_text;
    size_t text_size;
    char directory[SCRIPT_PATH_MAX];
    const char *label;
    char open_path[SCRIPT_PATH_MAX];
    FILE *open_file;
} ScriptRun;

typedef struct {
    CanHubSession *session;
    CanHubTxn *transaction;
    CanHubTxnScriptOptions options;
    uint32_t total_steps;
    uint64_t reported_at_us;
} Progress;

static int32_t runScript(ScriptRun *run, CanHubSession *session, const CanHubTxnScriptOptions *options, CanHubTxnReport *report);
static bool loadScript(ScriptRun *run, CanHubSession *session, const char *script_path);
static void keepDirectory(ScriptRun *run, const char *script_path);
static void keepBaseDirectory(ScriptRun *run, const char *base_directory);
static bool readScriptFile(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size);
static bool resolvePath(const ScriptRun *run, const char *path, char *resolved, size_t resolved_size);
static int32_t compileScript(ScriptRun *run, CanHubSession *session);
static int32_t prepareKeepalive(ScriptRun *run, CanHubSession *session);
static int32_t countSteps(ScriptRun *run, CanHubSession *session, uint32_t *total_steps);
static int32_t reportScriptError(CanHubSession *session, const ScriptRun *run, uint16_t line_number, const char *message);
static int32_t streamSteps(ScriptRun *run, Progress *progress);
static int32_t settle(Progress *progress);
static bool keepGoing(Progress *progress);
static void releaseRun(ScriptRun *run);

/* ---------- public ---------- */

int32_t canhub_txn_run_script(
    CanHubSession *session,
    const char *text,
    const char *base_directory,
    const CanHubTxnScriptOptions *options,
    CanHubTxnReport *report
)
{
    ScriptRun *run;
    int32_t result;

    if (session == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }
    if (text == NULL || report == NULL) {
        CanHubSession_SetError(session, "script text and report required");
        return CANHUB_ERR_ARGUMENT;
    }

    run = calloc(1, sizeof(*run));
    if (run == NULL) {
        CanHubSession_SetError(session, "out of memory for the script");
        return CANHUB_ERR_STATE;
    }
    run->text = text;
    run->text_size = strlen(text);
    keepBaseDirectory(run, base_directory);
    run->label = TEXT_LABEL;

    result = runScript(run, session, options, report);
    releaseRun(run);
    return result;
}

int32_t canhub_txn_run_script_file(
    CanHubSession *session,
    const char *path,
    const CanHubTxnScriptOptions *options,
    CanHubTxnReport *report
)
{
    ScriptRun *run;
    int32_t result;

    if (session == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }
    if (path == NULL || report == NULL) {
        CanHubSession_SetError(session, "script path and report required");
        return CANHUB_ERR_ARGUMENT;
    }

    run = calloc(1, sizeof(*run));
    if (run == NULL) {
        CanHubSession_SetError(session, "out of memory for the script");
        return CANHUB_ERR_STATE;
    }
    run->label = path;

    result = loadScript(run, session, path) ? runScript(run, session, options, report) : CANHUB_ERR_ARGUMENT;
    releaseRun(run);
    return result;
}

/* ---------- private ---------- */

static int32_t runScript(ScriptRun *run, CanHubSession *session, const CanHubTxnScriptOptions *options, CanHubTxnReport *report)
{
    Progress progress;
    CanHubTxnOptions transaction_options;
    char message[SCRIPT_DETAIL_SIZE];
    int32_t result;

    memset(report, 0, sizeof(*report));
    memset(&progress, 0, sizeof(progress));
    progress.session = session;
    if (options != NULL) {
        progress.options = *options;
    }

    result = compileScript(run, session);
    if (result == CANHUB_OK) {
        result = prepareKeepalive(run, session);
    }
    if (result == CANHUB_OK) {
        result = countSteps(run, session, &progress.total_steps);
    }
    if (result != CANHUB_OK) {
        return result;
    }

    transaction_options = run->program.options;
    transaction_options.on_roster = progress.options.on_roster;
    transaction_options.context = progress.options.context;
    progress.transaction = canhub_txn_create(session, &transaction_options);
    if (progress.transaction == NULL) {
        return CANHUB_ERR_STATE;
    }

    result = streamSteps(run, &progress);
    if (result == CANHUB_OK) {
        result = settle(&progress);
    }
    if (result == CANHUB_ERR_ARGUMENT) {
        snprintf(message, sizeof(message), "%s", CanHubSession_Error(session));
        canhub_txn_abort(progress.transaction);
        canhub_txn_finish(progress.transaction, report);
        return reportScriptError(session, run, run->generator.line_number, message);
    }
    return canhub_txn_finish(progress.transaction, report);
}

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

    run->owned_text = malloc((size_t)size + 1);
    if (run->owned_text == NULL || fread(run->owned_text, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        CanHubSession_SetError(session, "cannot read the script");
        return false;
    }
    fclose(file);

    run->owned_text[size] = '\0';
    run->text = run->owned_text;
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

static void keepBaseDirectory(ScriptRun *run, const char *base_directory)
{
    size_t length;

    run->directory[0] = '\0';
    if (base_directory == NULL || base_directory[0] == '\0') {
        return;
    }

    length = strlen(base_directory);
    if (base_directory[length - 1] == PATH_SEPARATOR || base_directory[length - 1] == WINDOWS_PATH_SEPARATOR) {
        snprintf(run->directory, sizeof(run->directory), "%s", base_directory);
        return;
    }
    snprintf(run->directory, sizeof(run->directory), "%s%c", base_directory, PATH_SEPARATOR);
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

static int32_t compileScript(ScriptRun *run, CanHubSession *session)
{
    ScriptError error;

    memset(&error, 0, sizeof(error));
    if (!ScriptParser_Parse(&run->script, run->text, run->text_size, &error) ||
        !ScriptCompiler_Compile(&run->program, &run->script, &error)) {
        return reportScriptError(session, run, error.line_number, error.message);
    }
    return CANHUB_OK;
}

static int32_t prepareKeepalive(ScriptRun *run, CanHubSession *session)
{
    ScriptFiles files = { run, readScriptFile };
    ScriptError error;

    if (run->script.keepalive == SCRIPT_NO_INDEX) {
        return CANHUB_OK;
    }

    memset(&error, 0, sizeof(error));
    ScriptGenerator_Start(&run->generator, &run->program, &files);
    if (!ScriptGenerator_Keepalive(&run->generator, &run->script.statements[run->script.keepalive], &run->keepalive, &error)) {
        return reportScriptError(session, run, error.line_number, error.message);
    }
    run->program.options.keepalive = &run->keepalive;
    return CANHUB_OK;
}

static int32_t countSteps(ScriptRun *run, CanHubSession *session, uint32_t *total_steps)
{
    ScriptFiles files = { run, readScriptFile };
    ScriptError error;
    CanHubTxnStep step;
    uint8_t generated;

    memset(&error, 0, sizeof(error));
    *total_steps = 0;
    TransactionBuilder_Init(&run->builder, run->program.options.bitrate_switch);
    ScriptGenerator_Start(&run->generator, &run->program, &files);
    while ((generated = ScriptGenerator_Next(&run->generator, &step, &error)) == kSCRIPT_GENERATED_STEP) {
        if (!TransactionBuilder_Step(&run->builder, &step, &run->build)) {
            return reportScriptError(session, run, run->generator.line_number, TransactionBuilder_Error(&run->builder));
        }
        (*total_steps)++;
    }
    if (generated == kSCRIPT_GENERATED_ERROR) {
        return reportScriptError(session, run, error.line_number, error.message);
    }
    if (*total_steps == 0) {
        CanHubSession_SetError(session, "the script has no step");
        return CANHUB_ERR_ARGUMENT;
    }
    return CANHUB_OK;
}

static int32_t reportScriptError(CanHubSession *session, const ScriptRun *run, uint16_t line_number, const char *message)
{
    char text[SCRIPT_MESSAGE_SIZE];

    snprintf(text, sizeof(text), "%s:%" PRIu16 ": %s", run->label, line_number, message);
    CanHubSession_SetError(session, text);
    return CANHUB_ERR_ARGUMENT;
}

static int32_t streamSteps(ScriptRun *run, Progress *progress)
{
    ScriptFiles files = { run, readScriptFile };
    ScriptError error;
    CanHubTxnStep step;
    uint8_t generated;
    int32_t result;

    memset(&error, 0, sizeof(error));
    ScriptGenerator_Start(&run->generator, &run->program, &files);
    while ((generated = ScriptGenerator_Next(&run->generator, &step, &error)) == kSCRIPT_GENERATED_STEP) {
        result = canhub_txn_step(progress->transaction, &step);
        if (result != CANHUB_OK) {
            return result;
        }
        if (!keepGoing(progress)) {
            canhub_txn_abort(progress->transaction);
            return CANHUB_ERR_TXN_ABORTED;
        }
    }
    if (generated == kSCRIPT_GENERATED_ERROR) {
        CanHubSession_SetError(progress->session, error.message);
        run->generator.line_number = error.line_number;
        return CANHUB_ERR_ARGUMENT;
    }
    return CANHUB_OK;
}

static int32_t settle(Progress *progress)
{
    bool cancelled = false;
    int32_t result;

    while ((result = canhub_txn_wait(progress->transaction, WAIT_SLICE_MS)) == CANHUB_ERR_TIMEOUT) {
        if (!cancelled && !keepGoing(progress)) {
            canhub_txn_abort(progress->transaction);
            cancelled = true;
        }
    }
    return result == CANHUB_ERR_DISCONNECTED ? result : CANHUB_OK;
}

static bool keepGoing(Progress *progress)
{
    CanHubTxnProgress snapshot;
    uint64_t now_us = Clock_MonotonicUs();

    if (progress->options.on_progress == NULL || now_us - progress->reported_at_us < PROGRESS_PERIOD_US) {
        return true;
    }
    progress->reported_at_us = now_us;
    canhub_txn_progress(progress->transaction, &snapshot);
    return progress->options.on_progress(progress->options.context, &snapshot, progress->total_steps) != 0;
}

static void releaseRun(ScriptRun *run)
{
    if (run->open_file != NULL) {
        fclose(run->open_file);
    }
    free(run->owned_text);
    free(run);
}
