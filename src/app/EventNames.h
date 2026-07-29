#pragma once

struct EventInfo;
class SC4Core;

const char* EventTypeDisplayName(unsigned type);
const char* EventDisplayName(const SC4Core& core, const EventInfo& event);
