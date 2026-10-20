#include "client/txn_script/script_error.h"

#include "shared/bounded_string.h"

/* ---------- public ---------- */

void ScriptError_Set(ScriptError *self, uint16_t line_number, const char *message)
{
    self->line_number = line_number;
    BoundedString_Copy(self->message, sizeof(self->message), message);
}
