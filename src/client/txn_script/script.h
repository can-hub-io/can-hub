#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "client/txn_script/checksum.h"

#define SCRIPT_CONSTANTS_MAX 64
#define SCRIPT_TEMPLATES_MAX 16
#define SCRIPT_STATEMENTS_MAX 256
#define SCRIPT_CONDITIONS_MAX 320
#define SCRIPT_CONDITION_TERMS_MAX 8
#define SCRIPT_DATA_ITEMS_MAX 1024
#define SCRIPT_LIST_RANGES_MAX 16
#define SCRIPT_PATH_SIZE 128
#define SCRIPT_NO_INDEX 0xFFFF

/*
 * The syntax tree of a transaction script (doc/transaction-scripts.md), as the
 * parser builds it and before any name is resolved. Names point into the
 * script text, which must outlive the tree. Conditions and data bytes live in
 * pools the templates and statements index into.
 */
typedef struct {
    const char *text;
    uint8_t length;
} ScriptName;

typedef struct {
    uint64_t value;
    uint8_t hex_digits;
} ScriptNumber;

typedef struct {
    uint32_t first;
    uint32_t last;
} ScriptRange;

typedef struct {
    ScriptRange ranges[SCRIPT_LIST_RANGES_MAX];
    uint8_t range_count;
} ScriptList;

typedef struct {
    ScriptName name;
    ScriptList list;
} ScriptListReference;

typedef struct {
    ScriptNumber number;
    ScriptName name;
} ScriptValue;

typedef enum tscript_byte_order_e {
    kSCRIPT_BYTE_ORDER_LITTLE_ENDIAN = 0,
    kSCRIPT_BYTE_ORDER_BIG_ENDIAN = 1,
    kSCRIPT_BYTE_ORDER_MAX,
} TSCRIPT_BYTE_ORDER;

typedef struct {
    uint8_t order;
    ScriptName function;
    ScriptName data;
    ScriptNumber literal;
} ScriptCheck;

typedef enum tscript_field_e {
    kSCRIPT_FIELD_ID = 0,
    kSCRIPT_FIELD_BYTE = 1,
    kSCRIPT_FIELD_BYTES = 2,
    kSCRIPT_FIELD_MAX,
} TSCRIPT_FIELD;

typedef struct {
    uint8_t field;
    uint8_t first;
    uint8_t last;
    uint8_t shift;
    bool has_mask;
    ScriptNumber mask;
    ScriptValue value;
    ScriptCheck check;
    uint16_t line_number;
} ScriptTerm;

typedef struct {
    ScriptTerm terms[SCRIPT_CONDITION_TERMS_MAX];
    uint8_t count;
    uint16_t line_number;
} ScriptCondition;

typedef enum tscript_constant_e {
    kSCRIPT_CONSTANT_NUMBER = 0,
    kSCRIPT_CONSTANT_LIST = 1,
    kSCRIPT_CONSTANT_FILE = 2,
    kSCRIPT_CONSTANT_CRC = 3,
    kSCRIPT_CONSTANT_MAX,
} TSCRIPT_CONSTANT;

typedef struct {
    ScriptName name;
    uint16_t line_number;
    uint8_t kind;
    ScriptNumber number;
    ScriptList list;
    char path[SCRIPT_PATH_SIZE];
    ChecksumModel model;
} ScriptConstant;

typedef struct {
    ScriptName name;
    uint16_t line_number;
    uint16_t success;
    uint16_t failure;
    uint16_t pending;
    bool has_roster;
    uint8_t quorum;
    ScriptName roster_variable;
    ScriptListReference roster;
    bool mismatch_abort;
    bool has_timeout;
    uint32_t timeout_us;
    bool continue_on_timeout;
} ScriptTemplate;

typedef enum tscript_data_e {
    kSCRIPT_DATA_BYTE = 0,
    kSCRIPT_DATA_NAME = 1,
    kSCRIPT_DATA_MAX,
} TSCRIPT_DATA;

typedef struct {
    uint8_t kind;
    uint8_t byte;
    ScriptName name;
} ScriptDataItem;

typedef enum tscript_statement_e {
    kSCRIPT_STATEMENT_SEND = 0,
    kSCRIPT_STATEMENT_EXPECT = 1,
    kSCRIPT_STATEMENT_FOR = 2,
    kSCRIPT_STATEMENT_MAX,
} TSCRIPT_STATEMENT;

typedef enum tscript_iterable_e {
    kSCRIPT_ITERABLE_LIST = 0,
    kSCRIPT_ITERABLE_CHUNKS = 1,
    kSCRIPT_ITERABLE_MAX,
} TSCRIPT_ITERABLE;

typedef struct {
    uint8_t kind;
    uint16_t line_number;
    uint16_t first_child;
    uint16_t next_sibling;
    ScriptValue id;
    uint16_t first_data_item;
    uint8_t data_count;
    bool has_gap;
    uint32_t gap_us;
    bool has_expect;
    ScriptName template_name;
    uint16_t where;
    ScriptName variable;
    uint8_t iterable;
    ScriptListReference list;
    ScriptName chunk_source;
    uint32_t chunk_size;
    bool has_pad;
    uint8_t pad;
} ScriptStatement;

typedef struct {
    bool exclusive;
    uint16_t keepalive;
    uint32_t keepalive_period_us;
    uint32_t idle_timeout_us;
    ScriptConstant constants[SCRIPT_CONSTANTS_MAX];
    uint8_t constant_count;
    ScriptTemplate templates[SCRIPT_TEMPLATES_MAX];
    uint8_t template_count;
    ScriptCondition conditions[SCRIPT_CONDITIONS_MAX];
    uint16_t condition_count;
    ScriptDataItem data_items[SCRIPT_DATA_ITEMS_MAX];
    uint16_t data_item_count;
    ScriptStatement statements[SCRIPT_STATEMENTS_MAX];
    uint16_t statement_count;
    uint16_t first_statement;
} Script;

bool ScriptName_Is(const ScriptName *name, const char *word);
bool ScriptName_Equals(const ScriptName *name, const ScriptName *other);
