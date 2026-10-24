#include <cest>
#include <cstring>

extern "C" {
#include "client/txn_script/script_parser.h"
}

#include "script_examples.h"

static Script script;
static ScriptError error;

static bool parse(const char *text);
static const char *errorMessage(void);

describe("script_parser", []() {
    beforeEach([]() {
        memset(&error, 0, sizeof(error));
    });

    it("parses every example of the documentation", []() {
        const char *examples[] = {
            EXAMPLE_1, EXAMPLE_2, EXAMPLE_3, EXAMPLE_4, EXAMPLE_5,
            EXAMPLE_6, EXAMPLE_7, EXAMPLE_8, EXAMPLE_9, EXAMPLE_10,
        };
        uint8_t parsed = 0;
        uint8_t i;

        for(i=0; i<sizeof(examples) / sizeof(examples[0]); i++) {
            if (parse(examples[i])) {
                parsed++;
            }
        }

        expect(parsed).toBe(10);
    });

    it("reads a response with its success, timeout and continue", []() {
        bool parsed;
        ScriptResponse *response_node;

        parsed = parse(EXAMPLE_5);
        response_node = &script.responses[0];

        expect(parsed).toBe(true);
        expect(ScriptName_Is(&response_node->name, "optional-wake-up")).toBe(true);
        expect(response_node->timeout_us).toBe((uint32_t)200000);
        expect(response_node->continue_on_timeout).toBe(true);
        expect(script.statement_count).toBe(3);
        expect(script.statements[0].gap_us).toBe((uint32_t)10000);
        expect(script.statements[2].has_expect).toBe(true);
    });

    it("reads hexadecimal data bytes, numbers and names", []() {
        bool parsed;
        const ScriptStatement *send;

        parsed = parse(EXAMPLE_7);
        send = &script.statements[script.statements[script.first_statement].first_child];

        expect(parsed).toBe(true);
        expect(send->data_count).toBe(5);
        expect(script.data_items[send->first_data_item].byte).toBe(0x2B);
        expect(script.data_items[send->first_data_item + 4].kind).toBe((uint8_t)kSCRIPT_DATA_NAME);
        expect(ScriptName_Is(&send->id.name, "node")).toBe(true);
        expect(send->id.number.value).toBe((uint64_t)0x600);
    });

    it("reads a roster response and its quorum", []() {
        bool parsed;
        const ScriptResponse *response_node;
        const ScriptCondition *success;

        parsed = parse(EXAMPLE_10);
        response_node = &script.responses[0];
        success = &script.conditions[response_node->success];

        expect(parsed).toBe(true);
        expect(response_node->has_roster).toBe(true);
        expect(response_node->quorum).toBe(24);
        expect(ScriptName_Is(&response_node->roster_variable, "cell")).toBe(true);
        expect(success->count).toBe(3);
        expect(success->terms[2].first).toBe(2);
    });

    it("reads nested chunk loops and the checked bytes", []() {
        bool parsed;
        const ScriptStatement *outer;
        const ScriptStatement *inner;
        const ScriptStatement *acknowledgement;
        const ScriptTerm *checked;

        parsed = parse(EXAMPLE_8);
        outer = &script.statements[script.first_statement];
        inner = &script.statements[outer->first_child];
        acknowledgement = &script.statements[inner->next_sibling];
        checked = &script.conditions[acknowledgement->where].terms[0];

        expect(parsed).toBe(true);
        expect(outer->chunk_size).toBe((uint32_t)128);
        expect(inner->chunk_size).toBe((uint32_t)8);
        expect(checked->field).toBe((uint8_t)kSCRIPT_FIELD_BYTES);
        expect(checked->first).toBe(1);
        expect(checked->last).toBe(2);
        expect(ScriptName_Is(&checked->check.function, "sum16")).toBe(true);
    });

    it("reads a custom crc", []() {
        bool parsed;
        const ScriptConstant *vendor_crc;

        parsed = parse(EXAMPLE_9);
        vendor_crc = &script.constants[3];

        expect(parsed).toBe(true);
        expect(script.exclusive).toBe(true);
        expect(vendor_crc->kind).toBe((uint8_t)kSCRIPT_CONSTANT_CRC);
        expect(vendor_crc->model.polynomial).toBe((uint32_t)0x8005);
        expect(vendor_crc->model.reflect_input).toBe(true);
    });

    it("names the line of an unknown statement", []() {
        bool parsed;

        parsed = parse("REQUEST = 0x7E0\nsned REQUEST data 01\n");

        expect(parsed).toBe(false);
        expect(error.line_number).toBe(2);
        expect(errorMessage()).toBe("unknown statement");
    });

    it("refuses a block that is not indented", []() {
        bool parsed;

        parsed = parse("response answer\nsuccess id == 0x7E8\n");

        expect(parsed).toBe(false);
        expect(error.line_number).toBe(1);
    });

    it("refuses indentation that matches no block", []() {
        bool parsed;

        parsed = parse("response answer\n    success id == 0x7E8\n  timeout 50ms\n");

        expect(parsed).toBe(false);
        expect(error.line_number).toBe(3);
    });

    it("refuses tabs", []() {
        bool parsed;

        parsed = parse("response answer\n\tsuccess id == 0x7E8\n");

        expect(parsed).toBe(false);
        expect(errorMessage()).toBe("indent with spaces, not tabs");
    });

    it("refuses a response for without success", []() {
        bool parsed;

        parsed = parse("response alive\n    for node in 0..3\n        timeout 1s\n");

        expect(parsed).toBe(false);
    });

    it("refuses several bytes compared without a byte order", []() {
        bool parsed;

        parsed = parse("response ack\n    success byte[1..2] == 0x1234\n");

        expect(parsed).toBe(false);
        expect(errorMessage()).toBe("several bytes compare to little_endian(...) or big_endian(...)");
    });
});

static bool parse(const char *text)
{
    return ScriptParser_Parse(&script, text, strlen(text), &error);
}

static const char *errorMessage(void)
{
    return error.message;
}
