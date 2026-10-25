#include "client/txn_script/script_generator.h"

#include <string.h>

#include "protocol/frame_message.h"

#define MICROSECONDS_PER_MILLISECOND 1000

static const uint8_t FD_LENGTHS[] = { 12, 16, 20, 24, 32, 48, 64 };

static uint16_t *cursorOf(ScriptGenerator *self);
static bool enterFor(ScriptGenerator *self, uint16_t index, ScriptError *error, bool *entered);
static bool advanceFrame(ScriptGenerator *self, bool *more, ScriptError *error);
static bool loadChunk(ScriptGenerator *self, ScriptGeneratorFrame *frame, ScriptError *error);
static bool readFile(
    ScriptGenerator *self,
    const ScriptConstant *file,
    uint64_t offset,
    uint8_t *buffer,
    uint32_t size,
    uint32_t *read_size
);
static void rebuildBindings(ScriptGenerator *self);
static bool buildStep(ScriptGenerator *self, const ScriptStatement *statement, CanHubTxnStep *step, ScriptError *error);
static bool fillFrame(
    ScriptGenerator *self,
    const ScriptStatement *statement,
    CanHubCanId *id,
    uint8_t *data,
    uint8_t *length,
    ScriptError *error
);
static bool appendData(uint8_t *destination, uint8_t *length, const uint8_t *data, uint32_t size);
static bool isFrameLength(uint32_t length);

/* ---------- public ---------- */

void ScriptGenerator_Start(ScriptGenerator *self, const ScriptProgram *program, const ScriptFiles *files)
{
    memset(self, 0, sizeof(*self));
    self->program = program;
    self->files = *files;
    self->root_cursor = program->script->first_statement;
}

uint8_t ScriptGenerator_Next(ScriptGenerator *self, CanHubTxnStep *step, ScriptError *error)
{
    const ScriptStatement *statement;
    uint16_t *cursor;
    bool more;
    bool entered;

    for (;;) {
        cursor = cursorOf(self);
        if (*cursor == SCRIPT_NO_INDEX) {
            if (self->depth == 0) {
                return kSCRIPT_GENERATED_END;
            }
            if (!advanceFrame(self, &more, error)) {
                return kSCRIPT_GENERATED_ERROR;
            }
            continue;
        }

        statement = &self->program->script->statements[*cursor];
        if (statement->kind == kSCRIPT_STATEMENT_FOR) {
            if (!enterFor(self, *cursor, error, &entered)) {
                return kSCRIPT_GENERATED_ERROR;
            }
            if (!entered) {
                *cursor = statement->next_sibling;
            }
            continue;
        }

        *cursor = statement->next_sibling;
        return buildStep(self, statement, step, error) ? kSCRIPT_GENERATED_STEP : kSCRIPT_GENERATED_ERROR;
    }
}

bool ScriptGenerator_Keepalive(
    ScriptGenerator *self,
    const ScriptStatement *statement,
    CanHubTxnKeepalive *keepalive,
    ScriptError *error
)
{
    memset(keepalive, 0, sizeof(*keepalive));
    keepalive->every_ms = self->program->script->keepalive_period_us / MICROSECONDS_PER_MILLISECOND;
    return fillFrame(self, statement, &keepalive->id, keepalive->data, &keepalive->length, error);
}

/* ---------- private ---------- */

static uint16_t *cursorOf(ScriptGenerator *self)
{
    if (self->depth == 0) {
        return &self->root_cursor;
    }
    return &self->frames[self->depth - 1].cursor;
}

static bool enterFor(ScriptGenerator *self, uint16_t index, ScriptError *error, bool *entered)
{
    const ScriptStatement *statement = &self->program->script->statements[index];
    const ScriptConstant *constant;
    ScriptGeneratorFrame *frame;

    *entered = false;
    if (self->depth == SCRIPT_BINDINGS_MAX) {
        ScriptError_Set(error, statement->line_number, "loops nested too deep");
        return false;
    }

    frame = &self->frames[self->depth];
    frame->statement = index;
    frame->cursor = statement->first_child;
    frame->range = 0;
    frame->chunk_index = 0;
    if (statement->iterable == kSCRIPT_ITERABLE_LIST) {
        constant = ScriptResolver_Constant(self->program->script, &statement->list.name);
        frame->value = statement->list.name.length > 0 ? constant->list.ranges[0].first : statement->list.list.ranges[0].first;
        self->depth++;
        rebuildBindings(self);
        *entered = true;
        return true;
    }

    if (!loadChunk(self, frame, error)) {
        return false;
    }
    if (frame->chunk_size == 0) {
        return true;
    }

    self->depth++;
    rebuildBindings(self);
    *entered = true;
    return true;
}

static bool advanceFrame(ScriptGenerator *self, bool *more, ScriptError *error)
{
    ScriptGeneratorFrame *frame = &self->frames[self->depth - 1];
    const ScriptStatement *statement = &self->program->script->statements[frame->statement];
    const ScriptList *list = &statement->list.list;
    const ScriptConstant *constant;

    *more = false;
    if (statement->iterable == kSCRIPT_ITERABLE_LIST) {
        if (statement->list.name.length > 0) {
            constant = ScriptResolver_Constant(self->program->script, &statement->list.name);
            list = &constant->list;
        }
        if (frame->value < list->ranges[frame->range].last) {
            frame->value++;
            *more = true;
        } else if (frame->range + 1 < list->range_count) {
            frame->range++;
            frame->value = list->ranges[frame->range].first;
            *more = true;
        }
    } else {
        frame->chunk_index++;
        if (!loadChunk(self, frame, error)) {
            return false;
        }
        *more = frame->chunk_size > 0;
    }

    if (*more) {
        frame->cursor = statement->first_child;
        rebuildBindings(self);
        return true;
    }

    self->depth--;
    rebuildBindings(self);
    *cursorOf(self) = statement->next_sibling;
    return true;
}

static bool loadChunk(ScriptGenerator *self, ScriptGeneratorFrame *frame, ScriptError *error)
{
    const ScriptStatement *statement = &self->program->script->statements[frame->statement];
    const ScriptBinding *parent = ScriptResolver_Binding(&self->bindings, &statement->chunk_source);
    const ScriptConstant *file = ScriptResolver_Constant(self->program->script, &statement->chunk_source);
    uint64_t offset = (uint64_t)frame->chunk_index * statement->chunk_size;
    uint32_t read_size = 0;

    if (parent != NULL && parent->kind == kSCRIPT_BINDING_CHUNK) {
        if (offset < parent->size) {
            read_size = parent->size - (uint32_t)offset;
            read_size = read_size < statement->chunk_size ? read_size : statement->chunk_size;
            memcpy(frame->buffer, parent->data + offset, read_size);
        }
    } else if (!readFile(self, file, offset, frame->buffer, statement->chunk_size, &read_size)) {
        ScriptError_Set(error, statement->line_number, "could not read the file");
        return false;
    }

    if (read_size > 0 && read_size < statement->chunk_size && statement->has_pad) {
        memset(frame->buffer + read_size, statement->pad, statement->chunk_size - read_size);
        read_size = statement->chunk_size;
    }
    frame->chunk_size = read_size;
    return true;
}

static bool readFile(
    ScriptGenerator *self,
    const ScriptConstant *file,
    uint64_t offset,
    uint8_t *buffer,
    uint32_t size,
    uint32_t *read_size
)
{
    uint32_t piece;

    *read_size = 0;
    while (*read_size < size) {
        if (!self->files.read(
                self->files.context,
                file->path,
                offset + *read_size,
                buffer + *read_size,
                size - *read_size,
                &piece
            )) {
            return false;
        }
        if (piece == 0) {
            return true;
        }
        *read_size += piece;
    }
    return true;
}

static void rebuildBindings(ScriptGenerator *self)
{
    const ScriptStatement *statement;
    ScriptGeneratorFrame *frame;
    ScriptBinding *binding;
    uint8_t i;

    self->bindings.count = self->depth;
    for(i=0; i<self->depth; i++) {
        frame = &self->frames[i];
        statement = &self->program->script->statements[frame->statement];
        binding = &self->bindings.items[i];
        binding->name = statement->variable;
        if (statement->iterable == kSCRIPT_ITERABLE_LIST) {
            binding->kind = kSCRIPT_BINDING_VALUE;
            binding->value = frame->value;
            binding->data = NULL;
            binding->size = 0;
        } else {
            binding->kind = kSCRIPT_BINDING_CHUNK;
            binding->value = 0;
            binding->data = frame->buffer;
            binding->size = frame->chunk_size;
        }
    }
}

static bool buildStep(ScriptGenerator *self, const ScriptStatement *statement, CanHubTxnStep *step, ScriptError *error)
{
    ScriptScope scope = { self->program->script, &self->bindings, &self->files, NULL };
    uint16_t response;
    uint8_t length = 0;

    memset(step, 0, sizeof(*step));
    self->line_number = statement->line_number;
    if (!ScriptCompiler_StepShape(self->program, statement, &scope, &response, &self->where, error)) {
        return false;
    }

    step->gap_us = statement->gap_us;
    if (response != SCRIPT_TRANSMIT_ONLY) {
        step->response = &self->program->responses[response];
    }
    if (statement->where != SCRIPT_NO_INDEX) {
        step->where = &self->where;
    }
    if (statement->kind == kSCRIPT_STATEMENT_EXPECT) {
        return true;
    }
    if (!fillFrame(self, statement, &step->id, self->data, &length, error)) {
        return false;
    }
    step->data = self->data;
    step->length = length;
    return true;
}

static bool fillFrame(
    ScriptGenerator *self,
    const ScriptStatement *statement,
    CanHubCanId *id,
    uint8_t *data,
    uint8_t *length,
    ScriptError *error
)
{
    ScriptScope scope = { self->program->script, &self->bindings, NULL, NULL };
    const ScriptDataItem *item;
    const ScriptBinding *binding;
    const ScriptConstant *constant;
    uint8_t byte;
    bool appended;
    uint8_t i;

    *length = 0;
    error->line_number = statement->line_number;
    if (!ScriptResolver_Value(&scope, &statement->id, &id->value, &id->extended, error)) {
        return false;
    }

    for(i=0; i<statement->data_count; i++) {
        item = &self->program->script->data_items[statement->first_data_item + i];
        binding = ScriptResolver_Binding(&self->bindings, &item->name);
        constant = ScriptResolver_Constant(self->program->script, &item->name);
        if (item->kind == kSCRIPT_DATA_BYTE) {
            appended = appendData(data, length, &item->byte, 1);
        } else if (binding != NULL && binding->kind == kSCRIPT_BINDING_CHUNK) {
            appended = appendData(data, length, binding->data, binding->size);
        } else {
            byte = (uint8_t)(binding != NULL ? binding->value : constant->number.value);
            appended = appendData(data, length, &byte, 1);
        }
        if (!appended) {
            ScriptError_Set(error, statement->line_number, "a frame carries at most 64 bytes");
            return false;
        }
    }

    if (!isFrameLength(*length)) {
        ScriptError_Set(error, statement->line_number, "a frame over 8 bytes is CAN FD: 12, 16, 20, 24, 32, 48 or 64 bytes");
        return false;
    }
    return true;
}

static bool appendData(uint8_t *destination, uint8_t *length, const uint8_t *data, uint32_t size)
{
    if (*length + size > CANHUB_FRAME_PAYLOAD_MAX) {
        return false;
    }
    memcpy(destination + *length, data, size);
    *length = (uint8_t)(*length + size);
    return true;
}

static bool isFrameLength(uint32_t length)
{
    uint8_t i;

    if (length <= FRAME_PAYLOAD_MAX_CLASSIC) {
        return true;
    }
    for(i=0; i<sizeof(FD_LENGTHS); i++) {
        if (FD_LENGTHS[i] == length) {
            return true;
        }
    }
    return false;
}
