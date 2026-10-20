#include "client/txn_script/script.h"

#include <string.h>

/* ---------- public ---------- */

bool ScriptName_Is(const ScriptName *name, const char *word)
{
    size_t length = strlen(word);

    return name->length == length && memcmp(name->text, word, length) == 0;
}

bool ScriptName_Equals(const ScriptName *name, const ScriptName *other)
{
    return name->length == other->length && memcmp(name->text, other->text, name->length) == 0;
}
