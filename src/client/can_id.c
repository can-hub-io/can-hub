#include "client/can_id.h"

#include "protocol/frame_message.h"

/* ---------- public ---------- */

uint32_t CanId_Width(CanHubCanId id)
{
    return id.extended ? CANHUB_EXTENDED_ID_MASK : CANHUB_STANDARD_ID_MASK;
}

bool CanId_IsValid(CanHubCanId id)
{
    return id.value <= CanId_Width(id);
}

bool CanId_AreIgnoredBitsValid(CanHubCanId id, uint32_t ignored_bits)
{
    return (ignored_bits & ~CanId_Width(id)) == 0;
}

uint32_t CanId_ToWire(CanHubCanId id)
{
    return id.extended ? id.value | FRAME_CAN_ID_FLAG_EFF : id.value;
}
