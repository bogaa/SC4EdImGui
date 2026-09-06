 #include "EventNames.h"

#include "SC4Core.h"

#include <cstddef>

namespace {

struct EventName {
    unsigned id;
    const char* name;
};

static const EventName kEventNames[] = {
    { 0x01, "Fire" },
    { 0x02, "Bone" },
    { 0x03, "Ring" },
    { 0x04, "Pull Bridge" },
    { 0x05, "Go Behind" },
    { 0x06, "Platform" },
    { 0x07, "Medusa Head" },
    { 0x08, "Ghost" }, 
    { 0x09, "Porcupine" },
    { 0x0A, "Dog" }, 
    { 0x0B, "Bone Pillar" }, 
    { 0x0C, "Bat" },
    { 0x0D, "Secret Man" }, 
    { 0x0E, "Candle Main" }, 
    { 0x0F, "Book Bird" },
    { 0x10, "Bird" }, 
    { 0x11, "Skelly Walk" }, 
    { 0x12, "Skelly Bone" },
    { 0x13, "Empty" },
    { 0x14, "Pillar Exit" }, 
    { 0x15, "Exit" },
    { 0x16, "Crusher" }, 
    { 0x17, "Moving Platform" },
    { 0x2A, "Boss Loader" },
    { 0x2B, "Gate" },
    { 0x2C, "Wall Corps" }, 
    { 0x2D, "Small Flame" }, 
    { 0x2E, "Moon" },
    { 0x2F, "Breakable Block" }, 
    { 0x30, "Frog" }, 
    { 0x31, "Sword Skelly" },
    { 0x32, "Hanging Snakes" }, 
    { 0x33, "Coffin Sniper" }, 
    { 0x34, "Mud Man" },
    { 0x35, "Plant" }, 
    { 0x36, "Skelly High Five" }, 
    { 0x37, "Crumbling Block" },
    { 0x38, "Auto Spawner " },
    { 0x39, "Falling Pillar" }, 
    { 0x3A, "Falling Wood Bridge" }, 
    { 0x3B, "Turning Platform" },
    { 0x3C, "Leaf Man" }, 
    { 0x3D, "Big Flame" }, 
    { 0x3E, "Gargoyle" },
    { 0x3F, "Water Drip" }, 
    { 0x40, "Turning Platform Unused" },
    { 0x41, "Cam Lock" },
    { 0x42, "Table" }, 
    { 0x43, "Spider" },
    { 0x44, "Falling Stalactite" }, 
    { 0x45, "Object" },
    { 0x46, "Breakable Stairs" },
    { 0x47, "Donno" },
    { 0x48, "Special Events" },
    { 0x49, "Vines" },
    { 0x4A, "Turning Platform Spikes" },
    { 0x4B, "Unused Bat" }, 
    { 0x4C, "Fish Man Swimming" }, 
    { 0x4D, "Chandelier Falling" },
    { 0x4E, "Diving Bat" }, 
    { 0x4F, "Unknown" }, 
    { 0x50, "Splash" },
    { 0x51, "Fish Man Jumping" }, 
    { 0x52, "Axe Knight" }, 
    { 0x53, "Axe Projectile" },
    { 0x54, "Zombie Ghost" }, 
    { 0x55, "Bridge Rope" }, 
    { 0x56, "Whipping Skeleton" },
    { 0x57, "Hunchback" }, 
    { 0x58, "Harpie" }, 
    { 0x59, "Spear Knight" },
    { 0x5A, "Woman Ghost" }, 
    { 0x5B, "Ghost Man" }, 
    { 0x5C, "Hands With Sword Skelly" },
    { 0x5D, "Bone Dragon" }, 
    { 0x5E, "Bone Dragon" }, 
    { 0x5F, "Ectoplasm" },
    { 0x60, "Falling Dagger" }, 
    { 0x61, "Spike Swing" },
    { 0x62, "Moving Spikes" }, 
    { 0x63, "Gold Platform Splash" },
    { 0x64, "Secret Cave" }, 
    { 0x65, "Falling Blocks" },
    { 0x66, "Hand From Grave" },
    { 0x67, "Empty" },
    { 0x68, "Skulls Watcher" }, 
    { 0x69, "Red Skelly" },
    { 0x6A, "Stone Drop Splash" }, 
    { 0x6B, "Candle Dog" }, 
    { 0x6C, "Ceiling Skelly Mode 7" },
    { 0x6D, "Fuzzy Ball" }, 
    { 0x6E, "Stealing Hand" }, 
    { 0x6F, "Horse Head Fliped" },
    { 0x70, "Grave Digger" },
    { 0x71, "Horse Head Normal" }, 
    { 0x72, "Eye" }, 
    { 0x73, "Club Guy" },
    { 0x74, "Caterpillar" }, 
    { 0x75, "Shield Gargoyle" }, 
    { 0x76, "Dancing Couple" },
    { 0x77, "Empty" },
    { 0x78, "Mud Man Small" },
    { 0x79, "Mud Man Tinny" },
    { 0x7A, "Carpet Monster" }, 
    { 0x7B, "Coffin Circle" }, 
    { 0x7C, "Gear" },
    { 0x7D, "Stage B" }, 
    { 0x7E, "Headless Knight" }, 
    { 0x7F, "Rock Man" },
};

static const EventName kDropNames[] = {
    { 0x00, "Null" }, 
    { 0x18, "Small Heart" }, 
    { 0x19, "Big Heart" },
    { 0x1A, "Knife" }, 
    { 0x1B, "Axe" }, 
    { 0x1C, "Holy Water" },
    { 0x1D, "Cross" }, 
    { 0x1E, "Stopwatch" }, 
    { 0x1F, "Rosary" },
    { 0x20, "Potion" }, 
    { 0x21, "Whip Upgrade" }, 
    { 0x22, "Money 100" },
    { 0x23, "Double" }, 
    { 0x24, "Triple" }, 
    { 0x25, "Small Meat" },
    { 0x26, "Large Meat" }, 
    { 0x27, "Orb" }, 
    { 0x28, "1Up" },
    { 0x62, "Money 300" }, 
    { 0xA2, "Money 500" }, 
    { 0xE2, "Money 700" },
};

static const char* LookupName(const EventName* names, size_t count, unsigned id)
{
    for (size_t i = 0; i < count; ++i) {
        if (names[i].id == id) {
            return names[i].name;
        }
    }
    return nullptr;
}

}

const char* EventTypeDisplayName(unsigned type)
{
    switch (type) {
    case EVENT_TYPE_ENEMY: return "Enemy";
    case EVENT_TYPE_CANDLE: return "Candle";
    case EVENT_TYPE_OBJECT: return "Object";
    case EVENT_TYPE_SPECIAL: return "NotUsed";
    default: return "Unknown";
    }
}

const char* EventDisplayName(const SC4Core& core, const EventInfo& event)
{
    if (event.type == EVENT_TYPE_CANDLE) {
        if (const char* name = LookupName(kDropNames, std::size(kDropNames), event.eventId)) {
            return name;
        }
    }

    if (core.type == 2 && event.type == EVENT_TYPE_SPECIAL) {
        switch (event.eventId) {
        case 0x02: return "Dynamic tiles";
        case 0x03: return "Dynamic palette";
        case 0x08: return "Tile decompression";
        default: break;
        }
    }

    if (const char* name = LookupName(kEventNames, std::size(kEventNames), event.eventId)) {
        return name;
    }

    return EventTypeDisplayName(event.type);
}
