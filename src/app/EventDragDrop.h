#pragma once

#include <windows.h>

static constexpr const char* kSc4EventDragPayloadType = "SC4ED_EVENT_TEMPLATE";

#pragma pack(push, 1)
struct EventDragPayload {
    BYTE type;
    WORD eventId;
    WORD eventSubId;
    WORD unknown;
};
#pragma pack(pop)
