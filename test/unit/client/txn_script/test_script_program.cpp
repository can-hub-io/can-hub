#include <cest>
#include <cstring>

extern "C" {
#include "client/transaction_builder.h"
#include "client/txn_script/script_compiler.h"
#include "client/txn_script/script_generator.h"
#include "client/txn_script/script_parser.h"
#include "protocol/frame_message.h"
}

#include "script_examples.h"

#define STEPS_MAX 128
#define BATTERY_IMAGE_SIZE 256
#define INVERTER_IMAGE_SIZE 300
#define FRAME_BYTES_MAX 64

typedef struct {
    CanHubCanId id;
    bool sends;
    uint8_t data[FRAME_BYTES_MAX];
    uint8_t length;
    int32_t response;
    bool has_where;
    CanHubTxnMatch where;
    uint32_t gap_us;
    uint16_t line_number;
} GeneratedStep;

static Script script;
static ScriptProgram program;
static ScriptGenerator generator;
static ScriptError error;
static TransactionBuilder builder;
static TransactionBuild build;
static GeneratedStep steps[STEPS_MAX];
static TxnStep records[STEPS_MAX];
static uint16_t step_count;
static const char *refusal;
static ScriptFiles no_files = { NULL, NULL };
static uint8_t battery_image[BATTERY_IMAGE_SIZE];
static uint8_t inverter_image[INVERTER_IMAGE_SIZE];

static bool compile(const char *text);
static bool generate(void);
static bool buildAll(void);
static void keepStep(const CanHubTxnStep *step);
static CanHubTxnStep stepOf(const GeneratedStep *generated);
static bool readImage(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size);
static uint16_t sum16(const uint8_t *data, uint32_t size);
static uint16_t concatenateFrames(uint8_t *sent, uint16_t sent_max);

describe("script_program", []() {
    beforeEach([]() {
        uint16_t i;

        memset(&error, 0, sizeof(error));
        step_count = 0;
        refusal = NULL;
        for(i=0; i<BATTERY_IMAGE_SIZE; i++) {
            battery_image[i] = (uint8_t)i;
        }
        for(i=0; i<INVERTER_IMAGE_SIZE; i++) {
            inverter_image[i] = (uint8_t)(i * 7);
        }
    });

    it("turns one request and its answer into one response and one step", []() {
        bool ready;
        const CanHubTxnResponse *response;

        ready = compile(EXAMPLE_1) && generate();
        response = &program.responses[0];

        expect(ready).toBe(true);
        expect(response->success.id.value).toBe((uint32_t)0x7E8);
        expect(response->success.id.extended).toBe(false);
        expect(response->success.id_ignored_bits).toBe((uint32_t)0);
        expect(response->success.bytes[1].mask).toBe(0xFF);
        expect(response->success.bytes[1].value).toBe(0x50);
        expect(response->timeout_ms).toBe((uint32_t)50);
        expect(step_count).toBe(1);
        expect(steps[0].id.value).toBe((uint32_t)0x7E0);
        expect(steps[0].length).toBe(3);
        expect(steps[0].data[2]).toBe(0x03);
        expect(steps[0].response).toBe(0);
    });

    it("folds what a where adds into one template and a value per step", []() {
        bool ready;

        ready = compile(EXAMPLE_2) && generate() && buildAll();

        expect(ready).toBe(true);
        expect(builder.template_count).toBe(1);
        expect(builder.templates[0].patterns[kTXN_PATTERN_ADVANCE].payload_mask).toBe((uint64_t)0xFFFFFF00);
        expect(steps[0].where.id.value).toBe((uint32_t)0x7E8);
        expect(records[0].flags).toBe((uint8_t)TXN_STEP_FLAG_EXPECT_VALUE);
        expect(records[0].expect_value).toBe((uint64_t)0x90F16200);
        expect(records[1].expect_value).toBe((uint64_t)0x89F16200);
    });

    it("compiles failure, pending and mismatch, pending keeping the timeout", []() {
        bool ready;
        const CanHubTxnResponse *response;

        ready = compile(EXAMPLE_3);
        response = &program.responses[0];

        expect(ready).toBe(true);
        expect(response->failure != NULL).toBe(true);
        expect(response->failure->bytes[1].value).toBe(0x7F);
        expect(response->pending != NULL).toBe(true);
        expect(response->pending->bytes[3].value).toBe(0x78);
        expect(response->timeout_after_pending_ms).toBe((uint32_t)2000);
        expect(response->on_mismatch_abort).toBe(true);
    });

    it("takes a time for pending of its own", []() {
        bool ready;

        ready = compile(
            "response busy\n"
            "    success  id == 0x7E8 and byte[1] == 0x76\n"
            "    pending  id == 0x7E8 and byte[1] == 0x7F and byte[3] == 0x78\n"
            "    timeout  50ms\n"
            "    timeout after pending 5s\n"
            "send 0x7E0 data 01 36 expect busy\n"
        );

        expect(ready).toBe(true);
        expect(program.responses[0].timeout_ms).toBe((uint32_t)50);
        expect(program.responses[0].timeout_after_pending_ms).toBe((uint32_t)5000);
    });

    it("marks extended identifiers in responses and frames", []() {
        bool ready;
        const CanHubTxnResponse *response;

        ready = compile(EXAMPLE_4) && generate();
        response = &program.responses[0];

        expect(ready).toBe(true);
        expect(response->success.id.value).toBe((uint32_t)0x18DAF100);
        expect(response->success.id.extended).toBe(true);
        expect(response->success.id_ignored_bits).toBe((uint32_t)0xFF);
        expect(steps[0].id.value).toBe((uint32_t)0x18DA10F1);
        expect(steps[0].id.extended).toBe(true);
    });

    it("sends frames without an answer with their gap", []() {
        bool ready;

        ready = compile(EXAMPLE_5) && generate() && buildAll();

        expect(ready).toBe(true);
        expect(steps[0].response).toBe(-1);
        expect(steps[0].gap_us).toBe((uint32_t)10000);
        expect(program.responses[0].continue_on_timeout).toBe(true);
        expect(builder.template_count).toBe(2);
    });

    it("turns a response for into nodes told by the bits the id leaves free", []() {
        bool ready;
        const CanHubTxnResponse *response;

        ready = compile(EXAMPLE_6);
        response = &program.responses[0];

        expect(ready).toBe(true);
        expect(response->node_count).toBe((uint32_t)16);
        expect(response->nodes[15]).toBe((uint32_t)15);
        expect(response->node_from).toBe(CANHUB_TXN_NODE_IN_ID);
        expect(response->success.id.value).toBe((uint32_t)0x700);
        expect(response->success.id_ignored_bits).toBe((uint32_t)0x0F);
        expect(response->allowed_missing).toBe((uint32_t)0);
    });

    it("unrolls a list loop, one identifier per node", []() {
        bool ready;

        ready = compile(EXAMPLE_7) && generate() && buildAll();

        expect(ready).toBe(true);
        expect(step_count).toBe(16);
        expect(steps[5].id.value).toBe((uint32_t)0x605);
        expect(steps[5].data[4]).toBe(5);
        expect(steps[5].where.id.value).toBe((uint32_t)0x585);
        expect(builder.template_count).toBe(16);
    });

    it("streams a file in blocks of frames, each acknowledged with its sum", []() {
        bool ready;
        uint16_t first_sum;

        ready = compile(EXAMPLE_8) && generate() && buildAll();
        first_sum = sum16(battery_image, 128);

        expect(ready).toBe(true);
        expect(step_count).toBe(34);
        expect(steps[0].id.value).toBe((uint32_t)0x123);
        expect(steps[15].data[7]).toBe(127);
        expect(steps[16].sends).toBe(false);
        expect(records[16].flags).toBe((uint8_t)(TXN_STEP_FLAG_EXPECT_ONLY | TXN_STEP_FLAG_EXPECT_VALUE));
        expect(records[16].expect_value).toBe((uint64_t)(0x01 | ((uint64_t)first_sum << 8)));
        expect(program.node_count).toBe((uint32_t)32);
        expect(program.responses[0].success.id_ignored_bits).toBe((uint32_t)0x1F);
    });

    it("sends every byte of the image once and in order, frame by frame", []() {
        bool ready;
        uint8_t sent[BATTERY_IMAGE_SIZE];
        uint16_t sent_size;

        ready = compile(EXAMPLE_8) && generate();
        sent_size = concatenateFrames(sent, sizeof(sent));

        expect(ready).toBe(true);
        expect(sent_size).toBe(BATTERY_IMAGE_SIZE);
        expect(memcmp(sent, battery_image, BATTERY_IMAGE_SIZE)).toBe(0);
    });

    it("pads the last frame, checks blocks with a custom crc and the image with crc32", []() {
        bool ready;
        const GeneratedStep *last_frame;

        ready = compile(EXAMPLE_9) && generate() && buildAll();
        last_frame = &steps[step_count - 3];

        expect(ready).toBe(true);
        expect(program.options.exclusive).toBe(true);
        expect(builder.template_count).toBe(3);
        expect(step_count).toBe(32 + 1 + 6 + 1 + 1);
        expect(last_frame->data[7]).toBe(0xFF);
        expect(steps[0].id.value).toBe((uint32_t)0x18FF5000);
        expect(steps[0].id.extended).toBe(true);
        expect(records[step_count - 1].expect_value & 0xFF).toBe((uint64_t)0x02);
    });

    it("reads who answered from a data byte and needs a quorum", []() {
        bool ready;
        const CanHubTxnResponse *response;

        ready = compile(EXAMPLE_10);
        response = &program.responses[0];

        expect(ready).toBe(true);
        expect(response->node_from).toBe(CANHUB_TXN_NODE_IN_BYTE);
        expect(response->node_byte).toBe(2);
        expect(response->node_count).toBe((uint32_t)32);
        expect(response->allowed_missing).toBe((uint32_t)8);
    });

    it("carries the keepalive frame and the idle timeout to the options", []() {
        bool ready;
        CanHubTxnKeepalive keepalive;
        bool built;

        ready = compile(
            "REQUEST = 0x7E0\n"
            "idle timeout 60s\n"
            "keepalive REQUEST data 02 3E 80 every 2s\n"
            "send 0x123 data 01\n"
        );
        ScriptGenerator_Start(&generator, &program, &no_files);
        built = ScriptGenerator_Keepalive(&generator, &script.statements[script.keepalive], &keepalive, &error);

        expect(ready).toBe(true);
        expect(program.options.idle_timeout_ms).toBe((uint32_t)60000);
        expect(built).toBe(true);
        expect(keepalive.every_ms).toBe((uint32_t)2000);
        expect(keepalive.id.value).toBe((uint32_t)0x7E0);
        expect(keepalive.length).toBe(3);
        expect(keepalive.data[1]).toBe(0x3E);
    });

    it("gives the options the agent's idle timeout when the script names none", []() {
        bool ready;

        ready = compile("send 0x123 data 01\n");

        expect(ready).toBe(true);
        expect(program.options.idle_timeout_ms).toBe((uint32_t)SCRIPT_DEFAULT_IDLE_TIMEOUT_MS);
    });

    it("refuses a second keepalive", []() {
        bool ready;

        ready = compile(
            "keepalive 0x7E0 data 02 3E 80 every 2s\n"
            "keepalive 0x7E1 data 02 3E 80 every 2s\n"
        );

        expect(ready).toBe(false);
        expect(error.line_number).toBe(2);
    });

    it("refuses a response without a timeout", []() {
        bool ready;

        ready = compile("response answer\n    success id == 0x7E8\nsend 0x7E0 data 01 expect answer\n");

        expect(ready).toBe(false);
        expect(error.line_number).toBe(1);
        expect((const char *)error.message).toBe("a response needs a timeout");
    });

    it("refuses a success that does not compare the id", []() {
        bool ready;

        ready = compile("response answer\n    success byte[0] == 0x01\n    timeout 10ms\n");

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("a success line compares the id");
    });

    it("refuses an expect naming no response", []() {
        bool ready;

        ready = compile("send 0x7E0 data 01 expect answer\n");

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("no response has this name");
    });

    it("leaves more than 16 ways of waiting to the builder, with the step's line", []() {
        bool ready;
        bool built;

        ready = compile(
            "response answer\n    success id & 0x700 == 0x500\n    timeout 10ms\n"
            "for node in 0..16\n    send 0x600 + node data 01 expect answer where id == 0x580 + node\n"
        ) && generate();
        built = buildAll();

        expect(ready).toBe(true);
        expect(built).toBe(false);
        expect(refusal != NULL).toBe(true);
        expect(steps[16].line_number).toBe(5);
    });

    it("refuses a list value that does not fit the mask", []() {
        bool ready;

        ready = compile(
            "response alive\n    for node in 0..40\n        success id & 0x7E0 == 0x700 and id & 0x1F == node\n    timeout 1s\n"
            "send 0x7FF data 01 expect alive\n"
        );

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("a value of the list does not fit the mask");
    });

    it("refuses nodes when other bits of the id are left free", []() {
        bool ready;

        ready = compile(
            "response alive\n    for node in 0..15\n        success id & 0x0F == node\n    timeout 1s\n"
            "send 0x7FF data 01 expect alive\n"
        );

        expect(ready).toBe(false);
        expect(error.line_number).toBe(3);
    });

    it("refuses nodes told by part of a data byte", []() {
        bool ready;

        ready = compile(
            "response alive\n    for cell in 0..15\n        success id == 0x650 and byte[2] & 0x0F == cell\n    timeout 1s\n"
            "send 0x640 data 01 expect alive\n"
        );

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("a node told by a data byte is the whole byte, such as byte[2] == cell");
    });

    it("refuses conditions that contradict each other", []() {
        bool ready;

        ready = compile("response answer\n    success id == 0x7E8 and id == 0x7E9\n    timeout 10ms\n");

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("the conditions on id contradict each other");
    });

    it("refuses a frame whose length CAN FD does not have", []() {
        bool ready;

        ready = compile("send 0x123 data 01 02 03 04 05 06 07 08 09\n") && generate();

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("a frame over 8 bytes is CAN FD: 12, 16, 20, 24, 32, 48 or 64 bytes");
    });
});

static bool compile(const char *text)
{
    if (!ScriptParser_Parse(&script, text, strlen(text), &error)) {
        return false;
    }
    return ScriptCompiler_Compile(&program, &script, &error);
}

static bool generate(void)
{
    ScriptFiles files = { NULL, readImage };
    CanHubTxnStep step;
    uint8_t result;

    ScriptGenerator_Start(&generator, &program, &files);
    while ((result = ScriptGenerator_Next(&generator, &step, &error)) == kSCRIPT_GENERATED_STEP) {
        keepStep(&step);
    }
    return result == kSCRIPT_GENERATED_END;
}

static bool buildAll(void)
{
    CanHubTxnStep step;
    uint16_t i;

    TransactionBuilder_Init(&builder, false);
    for(i=0; i<step_count; i++) {
        step = stepOf(&steps[i]);
        if (!TransactionBuilder_Step(&builder, &step, &build)) {
            refusal = TransactionBuilder_Error(&builder);
            return false;
        }
        records[i] = build.records[0];
    }
    return true;
}

static void keepStep(const CanHubTxnStep *step)
{
    GeneratedStep *kept = &steps[step_count++];

    memset(kept, 0, sizeof(*kept));
    kept->id = step->id;
    kept->sends = step->data != NULL;
    if (kept->sends) {
        memcpy(kept->data, step->data, step->length);
    }
    kept->length = (uint8_t)step->length;
    kept->response = step->response != NULL ? (int32_t)(step->response - program.responses) : -1;
    kept->has_where = step->where != NULL;
    if (kept->has_where) {
        kept->where = *step->where;
    }
    kept->gap_us = step->gap_us;
    kept->line_number = generator.line_number;
}

static CanHubTxnStep stepOf(const GeneratedStep *generated)
{
    CanHubTxnStep step;

    memset(&step, 0, sizeof(step));
    step.id = generated->id;
    step.data = generated->sends ? generated->data : NULL;
    step.length = generated->length;
    step.response = generated->response >= 0 ? &program.responses[generated->response] : NULL;
    step.where = generated->has_where ? &generated->where : NULL;
    step.gap_us = generated->gap_us;
    return step;
}

static bool readImage(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size)
{
    const uint8_t *image = battery_image;
    uint32_t image_size = BATTERY_IMAGE_SIZE;

    (void)context;
    if (strcmp(path, "inverter.bin") == 0) {
        image = inverter_image;
        image_size = INVERTER_IMAGE_SIZE;
    }
    *read_size = 0;
    if (offset < image_size) {
        *read_size = image_size - (uint32_t)offset < size ? image_size - (uint32_t)offset : size;
        memcpy(buffer, image + offset, *read_size);
    }
    return true;
}

static uint16_t sum16(const uint8_t *data, uint32_t size)
{
    uint32_t total = 0;
    uint32_t i;

    for(i=0; i<size; i++) {
        total += data[i];
    }
    return (uint16_t)total;
}

static uint16_t concatenateFrames(uint8_t *sent, uint16_t sent_max)
{
    uint16_t sent_size = 0;
    uint16_t i;

    for(i=0; i<step_count; i++) {
        if (!steps[i].sends || sent_size + steps[i].length > sent_max) {
            continue;
        }
        memcpy(sent + sent_size, steps[i].data, steps[i].length);
        sent_size += steps[i].length;
    }
    return sent_size;
}
