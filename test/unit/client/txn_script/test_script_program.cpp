#include <cest>
#include <cstring>

extern "C" {
#include "client/txn_script/script_compiler.h"
#include "client/txn_script/script_generator.h"
#include "client/txn_script/script_parser.h"
#include "protocol/frame_message.h"
}

#include "script_examples.h"

#define STEPS_MAX 128
#define BATTERY_IMAGE_SIZE 256
#define INVERTER_IMAGE_SIZE 300
#define NO_FILE (-1)

static Script script;
static ScriptProgram program;
static ScriptGenerator generator;
static ScriptError error;
static TxnStep steps[STEPS_MAX];
static uint16_t step_count;
static uint8_t battery_image[BATTERY_IMAGE_SIZE];
static uint8_t inverter_image[INVERTER_IMAGE_SIZE];

static bool compile(const char *text);
static bool generate(void);
static bool readImage(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size);
static uint16_t sum16(const uint8_t *data, uint32_t size);
static const TxnTemplateMessage *variantOf(const TxnStep *step);

describe("script_program", []() {
    beforeEach([]() {
        uint16_t i;

        memset(&error, 0, sizeof(error));
        step_count = 0;
        for(i=0; i<BATTERY_IMAGE_SIZE; i++) {
            battery_image[i] = (uint8_t)i;
        }
        for(i=0; i<INVERTER_IMAGE_SIZE; i++) {
            inverter_image[i] = (uint8_t)(i * 7);
        }
    });

    it("turns one request and its answer into one step", []() {
        bool ready;
        const TxnPattern *advance;

        ready = compile(EXAMPLE_1) && generate();
        advance = &program.variants[0].patterns[kTXN_PATTERN_ADVANCE];

        expect(ready).toBe(true);
        expect(program.variant_count).toBe(1);
        expect(program.variants[0].timeout_ms).toBe((uint32_t)50);
        expect(advance->can_id).toBe((uint32_t)0x7E8);
        expect(advance->can_mask).toBe((uint32_t)(0x7FF | FRAME_CAN_ID_FLAG_EFF));
        expect(advance->payload_mask).toBe((uint64_t)0xFF00);
        expect(advance->payload_value).toBe((uint64_t)0x5000);
        expect(step_count).toBe(1);
        expect(steps[0].can_id).toBe((uint32_t)0x7E0);
        expect(steps[0].payload_length).toBe(3);
        expect(steps[0].payload[2]).toBe(0x03);
    });

    it("folds what a where adds into one variant and a value per step", []() {
        bool ready;

        ready = compile(EXAMPLE_2) && generate();

        expect(ready).toBe(true);
        expect(program.variant_count).toBe(1);
        expect(program.variants[0].patterns[kTXN_PATTERN_ADVANCE].payload_mask).toBe((uint64_t)0xFFFFFF00);
        expect(steps[0].flags).toBe((uint8_t)TXN_STEP_FLAG_EXPECT_VALUE);
        expect(steps[0].expect_value).toBe((uint64_t)0x90F16200);
        expect(steps[1].expect_value).toBe((uint64_t)0x89F16200);
    });

    it("compiles failure, pending and mismatch into the abort and extend patterns", []() {
        bool ready;
        const TxnTemplateMessage *variant;

        ready = compile(EXAMPLE_3);
        variant = &program.variants[0];

        expect(ready).toBe(true);
        expect(variant->patterns_present).toBe((uint8_t)0x07);
        expect(variant->flags).toBe((uint8_t)TXN_TEMPLATE_FLAG_MISMATCH_ABORT);
        expect(variant->patterns[kTXN_PATTERN_EXTEND].payload_mask).toBe((uint64_t)0xFF00FF00);
        expect(variant->patterns[kTXN_PATTERN_EXTEND].payload_value).toBe((uint64_t)0x78007F00);
    });

    it("marks extended identifiers in patterns and frames", []() {
        bool ready;
        const TxnPattern *advance;

        ready = compile(EXAMPLE_4) && generate();
        advance = &program.variants[0].patterns[kTXN_PATTERN_ADVANCE];

        expect(ready).toBe(true);
        expect(advance->can_id).toBe((uint32_t)(0x18DAF100 | FRAME_CAN_ID_FLAG_EFF));
        expect(advance->can_mask).toBe((uint32_t)(0x1FFFFF00 | FRAME_CAN_ID_FLAG_EFF));
        expect(steps[0].can_id).toBe((uint32_t)(0x18DA10F1 | FRAME_CAN_ID_FLAG_EFF));
    });

    it("gives frames without an answer a spacing template of their own", []() {
        bool ready;

        ready = compile(EXAMPLE_5) && generate();

        expect(ready).toBe(true);
        expect(program.variant_count).toBe(2);
        expect(variantOf(&steps[0])->advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_NONE);
        expect(variantOf(&steps[0])->gap_us).toBe((uint32_t)10000);
        expect(variantOf(&steps[2])->flags).toBe((uint8_t)TXN_TEMPLATE_FLAG_SOFT);
    });

    it("turns a template for into the roster and how to tell who answered", []() {
        bool ready;

        ready = compile(EXAMPLE_6);

        expect(ready).toBe(true);
        expect(program.begin.roster_count).toBe(16);
        expect(program.begin.origin.source).toBe((uint8_t)kTXN_ORIGIN_SOURCE_CAN_ID);
        expect(program.begin.origin.mask).toBe((uint32_t)0x0F);
        expect(program.variants[0].advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_ROSTER_ALL);
        expect(program.variants[0].patterns[kTXN_PATTERN_ADVANCE].can_mask).toBe((uint32_t)(0x7F0 | FRAME_CAN_ID_FLAG_EFF));
    });

    it("unrolls a list loop, one identifier per node", []() {
        bool ready;

        ready = compile(EXAMPLE_7) && generate();

        expect(ready).toBe(true);
        expect(program.variant_count).toBe(16);
        expect(step_count).toBe(16);
        expect(steps[5].can_id).toBe((uint32_t)0x605);
        expect(steps[5].payload[4]).toBe(5);
        expect(variantOf(&steps[5])->patterns[kTXN_PATTERN_ADVANCE].can_id).toBe((uint32_t)0x585);
    });

    it("streams a file in blocks of frames, each acknowledged with its sum", []() {
        bool ready;
        uint16_t first_sum;

        ready = compile(EXAMPLE_8) && generate();
        first_sum = sum16(battery_image, 128);

        expect(ready).toBe(true);
        expect(step_count).toBe(34);
        expect(steps[0].can_id).toBe((uint32_t)0x123);
        expect(steps[15].payload[7]).toBe(127);
        expect(steps[16].flags).toBe((uint8_t)(TXN_STEP_FLAG_EXPECT_ONLY | TXN_STEP_FLAG_EXPECT_VALUE));
        expect(steps[16].expect_value).toBe((uint64_t)(0x01 | ((uint64_t)first_sum << 8)));
        expect(program.begin.roster_count).toBe(32);
        expect(program.begin.origin.mask).toBe((uint32_t)0x1F);
    });

    it("pads the last frame, checks blocks with a custom crc and the image with crc32", []() {
        bool ready;
        const TxnStep *last_frame;
        const TxnStep *verify;

        ready = compile(EXAMPLE_9) && generate();
        last_frame = &steps[step_count - 3];
        verify = &steps[step_count - 1];

        expect(ready).toBe(true);
        expect(program.begin.flags).toBe((uint8_t)TXN_BEGIN_FLAG_EXCLUSIVE);
        expect(program.variant_count).toBe(3);
        expect(step_count).toBe(32 + 1 + 6 + 1 + 1);
        expect(last_frame->payload[7]).toBe(0xFF);
        expect(steps[0].can_id).toBe((uint32_t)(0x18FF5000 | FRAME_CAN_ID_FLAG_EFF));
        expect(verify->expect_value & 0xFF).toBe((uint64_t)0x02);
    });

    it("reads who answered from a data byte and needs a quorum", []() {
        bool ready;

        ready = compile(EXAMPLE_10);

        expect(ready).toBe(true);
        expect(program.begin.origin.source).toBe((uint8_t)kTXN_ORIGIN_SOURCE_PAYLOAD);
        expect(program.begin.origin.payload_offset).toBe(2);
        expect(program.variants[0].advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_ROSTER_K);
        expect(program.variants[0].count_k).toBe(24);
    });

    it("refuses a template without a timeout", []() {
        bool ready;

        ready = compile("template response\n    success id == 0x7E8\nsend 0x7E0 data 01 expect response\n");

        expect(ready).toBe(false);
        expect(error.line_number).toBe(1);
        expect((const char *)error.message).toBe("a template needs a timeout");
    });

    it("refuses an expect naming no template", []() {
        bool ready;

        ready = compile("send 0x7E0 data 01 expect response\n");

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("no template has this name");
    });

    it("refuses more than 16 template variants", []() {
        bool ready;

        ready = compile(
            "template answer\n    success id & 0x700 == 0x500\n    timeout 10ms\n"
            "for node in 0..16\n    send 0x600 + node data 01 expect answer where id == 0x580 + node\n"
        );

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("more than 16 templates, counting what where and gap add to them");
    });

    it("refuses a list value that does not fit the mask", []() {
        bool ready;

        ready = compile(
            "template alive\n    for node in 0..40\n        success id & 0x1F == node\n    timeout 1s\n"
            "send 0x7FF data 01 expect alive\n"
        );

        expect(ready).toBe(false);
        expect((const char *)error.message).toBe("a value of the list does not fit the mask");
    });

    it("refuses conditions that contradict each other", []() {
        bool ready;

        ready = compile("template answer\n    success id == 0x7E8 and id == 0x7E9\n    timeout 10ms\n");

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
    uint8_t result;

    ScriptGenerator_Start(&generator, &program, &files);
    while ((result = ScriptGenerator_Next(&generator, &steps[step_count], &error)) == kSCRIPT_GENERATED_STEP) {
        step_count++;
    }
    return result == kSCRIPT_GENERATED_END;
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

static const TxnTemplateMessage *variantOf(const TxnStep *step)
{
    return &program.variants[step->template_id];
}
