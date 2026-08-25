#include "RomSession.h"

#include "SC4Core.h"

#include <algorithm>
#include <cstring>

RomSession::RomSession()
    : core_(std::make_unique<SC4Core>())
{
}

RomSession::~RomSession() = default;

SC4Core& RomSession::Core()
{
    return *core_;
}

const SC4Core& RomSession::Core() const
{
    return *core_;
}

int RomSession::LevelWidth() const
{
    return static_cast<int>(core_->levelWidth);
}

int RomSession::LevelHeight() const
{
    return static_cast<int>(core_->levelHeight);
}

int RomSession::CurrentLevel() const
{
    return static_cast<int>(core_->level);
}

int RomSession::Region() const
{
    return static_cast<int>(core_->region);
}

bool RomSession::IsExpandedRom() const
{
    return core_->expandedROM;
}

void RomSession::BeginEdit()
{
    if (!loaded_) {
        return;
    }
    revision_ = nextRevision_++;
    dirty_ = revision_ != savedRevision_;
}

std::vector<uint8_t> RomSession::CurrentRomBytes()
{
    if (!loaded_ || !core_->rom) {
        return {};
    }

    core_->SaveEvents();
    core_->SaveLevel();
    const BYTE* fileStart = core_->rom - core_->dummyHeader;
    return std::vector<uint8_t>(fileStart, fileStart + core_->romSize);
}

unsigned RomSession::ReadRom(unsigned snesAddress, int byteCount) const
{
    if (!loaded_ || snesAddress == 0 || byteCount <= 0) {
        return 0;
    }

    const BYTE* p = core_->rom + SNESCore::snes2pc(static_cast<int>(snesAddress));
    switch (byteCount) {
    case 1: return *p;
    case 2: return *reinterpret_cast<const WORD*>(p);
    case 4: return *reinterpret_cast<const DWORD*>(p);
    default: return 0;
    }
}

void RomSession::WriteRom(unsigned snesAddress, int byteCount, unsigned value)
{
    if (!loaded_ || snesAddress == 0 || byteCount <= 0) {
        return;
    }

    WriteRomPc(SNESCore::snes2pc(static_cast<int>(snesAddress)), byteCount, value);
}

void RomSession::WriteRomPc(unsigned pcOffset, int byteCount, unsigned value)
{
    if (!loaded_ || !core_->rom || byteCount <= 0 || pcOffset > core_->romSize || static_cast<unsigned>(byteCount) > core_->romSize - pcOffset) {
        return;
    }

    BYTE* p = core_->rom + pcOffset;
    switch (byteCount) {
    case 1:
        *p = static_cast<BYTE>(value & 0xFF);
        break;
    case 2:
        *reinterpret_cast<WORD*>(p) = static_cast<WORD>(value & 0xFFFF);
        break;
    case 4:
        *reinterpret_cast<DWORD*>(p) = static_cast<DWORD>(value);
        break;
    default:
        return;
    }
    BeginEdit();
}

void RomSession::WriteRomAll(const std::vector<unsigned>& snesAddresses, int byteCount, unsigned value)
{
    for (unsigned address : snesAddresses) {
        WriteRom(address, byteCount, value);
    }
}

RomUndoSnapshot RomSession::CreateUndoSnapshot(int selectedEventIndex) const
{
    RomUndoSnapshot snapshot = {};
    if (!loaded_ || !core_->rom) {
        return snapshot;
    }

    const BYTE* fileStart = core_->rom - core_->dummyHeader;
    snapshot.rom.assign(fileStart, fileStart + core_->romSize);
    snapshot.ram.assign(core_->ram, core_->ram + sizeof(core_->ram));
    snapshot.vram.assign(core_->vram, core_->vram + sizeof(core_->vram));
    snapshot.vramCache.assign(core_->vramCache, core_->vramCache + sizeof(core_->vramCache));
    snapshot.spriteCache.assign(core_->spriteCache, core_->spriteCache + sizeof(core_->spriteCache));
    snapshot.mapping.assign(core_->mapping, core_->mapping + (32 * 32 * 0x20 * 0x20));
    snapshot.events.reserve(core_->eventTable.size());
    for (const EventInfo& event : core_->eventTable) {
        EventUndoSnapshot copy = {};
        copy.match = event.match;
        copy.type = event.type;
        copy.xpos = event.xpos;
        copy.ypos = event.ypos;
        copy.eventId = event.eventId;
        copy.eventSubId = event.eventSubId;
        copy.spawnIndex = event.spawnIndex;
        copy.unknown = event.unknown;
        snapshot.events.push_back(copy);
    }
    snapshot.spriteUpdate.assign(core_->spriteUpdate.begin(), core_->spriteUpdate.end());
    snapshot.simonSpriteUpdate.assign(core_->simonSpriteUpdate.begin(), core_->simonSpriteUpdate.end());
    snapshot.selectedEventIndex = selectedEventIndex;
    snapshot.revision = revision_;
    return snapshot;
}

void RomSession::RestoreUndoSnapshot(const RomUndoSnapshot& snapshot, int& selectedEventIndex)
{
    if (!loaded_ || !core_->rom || snapshot.rom.empty()) {
        return;
    }

    const size_t romSize = (std::min)(static_cast<size_t>(core_->romSize), snapshot.rom.size());
    BYTE* fileStart = core_->rom - core_->dummyHeader;
    std::memcpy(fileStart, snapshot.rom.data(), romSize);
    if (snapshot.ram.size() == sizeof(core_->ram)) {
        std::memcpy(core_->ram, snapshot.ram.data(), sizeof(core_->ram));
    }
    if (snapshot.vram.size() == sizeof(core_->vram)) {
        std::memcpy(core_->vram, snapshot.vram.data(), sizeof(core_->vram));
    }
    if (snapshot.vramCache.size() == sizeof(core_->vramCache)) {
        std::memcpy(core_->vramCache, snapshot.vramCache.data(), sizeof(core_->vramCache));
    }
    if (snapshot.spriteCache.size() == sizeof(core_->spriteCache)) {
        std::memcpy(core_->spriteCache, snapshot.spriteCache.data(), sizeof(core_->spriteCache));
    }
    if (snapshot.mapping.size() == (32 * 32 * 0x20 * 0x20)) {
        std::memcpy(core_->mapping, snapshot.mapping.data(), snapshot.mapping.size() * sizeof(WORD));
    }

    core_->eventTable.clear();
    for (const EventUndoSnapshot& copy : snapshot.events) {
        EventInfo event = {};
        event.match = copy.match;
        event.type = copy.type;
        event.xpos = copy.xpos;
        event.ypos = copy.ypos;
        event.eventId = copy.eventId;
        event.eventSubId = copy.eventSubId;
        event.spawnIndex = copy.spawnIndex;
        event.unknown = copy.unknown;
        core_->eventTable.push_back(event);
    }
    core_->spriteUpdate.clear();
    core_->spriteUpdate.insert(snapshot.spriteUpdate.begin(), snapshot.spriteUpdate.end());
    core_->simonSpriteUpdate.clear();
    core_->simonSpriteUpdate.insert(snapshot.simonSpriteUpdate.begin(), snapshot.simonSpriteUpdate.end());
    selectedEventIndex = snapshot.selectedEventIndex;
    if (selectedEventIndex >= static_cast<int>(core_->eventTable.size())) {
        selectedEventIndex = core_->eventTable.empty() ? -1 : static_cast<int>(core_->eventTable.size()) - 1;
    }
    revision_ = snapshot.revision;
    dirty_ = revision_ != savedRevision_;
}

bool RomSession::OpenRom(const std::string& path)
{
    loaded_ = false;
    dirty_ = false;
    revision_ = 0;
    savedRevision_ = 0;
    nextRevision_ = 1;
    lastError_.clear();

    if (!LoadRomInfo(path, info_)) {
        lastError_ = "Could not read ROM file.";
        return false;
    }

    if (!core_->LoadNewRom(path.c_str())) {
        lastError_ = "SC4Core could not open the ROM.";
        return false;
    }

    if (!core_->CheckROM()) {
        lastError_ = "Unsupported ROM.";
        return false;
    }

    levelCount_ = static_cast<int>(core_->numLevels);
    loaded_ = LoadLevel(0, 0);
    return loaded_;
}

bool RomSession::Save()
{
    if (!loaded_) {
        lastError_ = "No ROM is loaded.";
        return false;
    }

    const std::vector<uint8_t> romBeforeSave(core_->rom, core_->rom + core_->romSize);
    core_->SaveEvents();
    core_->SaveLevel();
    if (!core_->spriteUpdate.empty() || !core_->simonSpriteUpdate.empty()) {
        std::memcpy(core_->rom, romBeforeSave.data(), romBeforeSave.size());
        lastError_ = "One or more edited sprite tiles did not fit in their ROM graphics packet. Expand the ROM and save again.";
        return false;
    }
    if (!core_->SaveRom(core_->filePath)) {
        lastError_ = "Could not save ROM.";
        return false;
    }

    LoadRomInfo(core_->filePath, info_);
    savedRevision_ = revision_;
    dirty_ = false;
    lastError_.clear();
    return true;
}

bool RomSession::SaveAs(const std::string& path)
{
    if (!loaded_) {
        lastError_ = "No ROM is loaded.";
        return false;
    }

    if (!core_->SaveAsRom(path.c_str())) {
        lastError_ = "Could not open target ROM file.";
        return false;
    }

    return Save();
}

bool RomSession::LoadLevel(int level, int checkpoint)
{
    if (levelCount_ <= 0) {
        return false;
    }

    level = std::clamp(level, 0, levelCount_ - 1);
    if (checkpoint < 0) {
        checkpoint = 0;
    }
    core_->SetLevel(static_cast<WORD>(level), static_cast<WORD>(checkpoint));
    core_->LoadLevel(false);
    return true;
}

bool RomSession::LoadCurrentLayer(bool background)
{
    if (!loaded_) {
        return false;
    }

    if (background) {
        core_->LoadBackground();
    } else {
        core_->LoadLevel(true);
    }
    return true;
}

bool RomSession::DeleteEvent(int& eventIndex)
{
    if (!loaded_ || eventIndex < 0 || eventIndex >= static_cast<int>(core_->eventTable.size())) {
        return false;
    }

    if (!core_->DelEvent(static_cast<unsigned>(eventIndex))) {
        lastError_ = "Could not delete event.";
        return false;
    }

    if (core_->eventTable.empty()) {
        eventIndex = -1;
    } else if (eventIndex >= static_cast<int>(core_->eventTable.size())) {
        eventIndex = static_cast<int>(core_->eventTable.size()) - 1;
    }

    BeginEdit();
    lastError_.clear();
    return true;
}

void RomSession::SortEvents()
{
    if (!loaded_) {
        return;
    }

    core_->SortEvents();
    core_->SaveEvents();
    BeginEdit();
}

void RomSession::SlotEvents()
{
    if (!loaded_) {
        return;
    }

    core_->SlotEvents();
    core_->SaveEvents();
    BeginEdit();
}

void RomSession::SaveEvents()
{
    if (!loaded_) {
        return;
    }

    core_->SaveEvents();
    BeginEdit();
}

bool RomSession::AddEvent(const EventInfo& event, int* eventIndex)
{
    if (!loaded_) {
        return false;
    }

    EventInfo copy = event;
    const unsigned insertIndex = static_cast<unsigned>(core_->eventTable.size());
    if (!core_->AddEvent(insertIndex, copy)) {
        lastError_ = "Could not add event.";
        return false;
    }

    if (eventIndex) {
        int index = 0;
        int foundIndex = -1;
        for (const EventInfo& current : core_->eventTable) {
            if (current.match == copy.match
                && current.type == copy.type
                && current.xpos == copy.xpos
                && current.ypos == copy.ypos
                && current.eventId == copy.eventId
                && current.eventSubId == copy.eventSubId
                && current.spawnIndex == copy.spawnIndex
                && current.unknown == copy.unknown) {
                foundIndex = index;
            }
            ++index;
        }
        if (foundIndex >= 0) {
            *eventIndex = foundIndex;
        }
    }

    BeginEdit();
    lastError_.clear();
    return true;
}

bool RomSession::ExpandRom()
{
    if (!loaded_) {
        lastError_ = "No ROM is loaded.";
        return false;
    }

    if (!core_->ExpandROM()) {
        lastError_ = "ROM expansion failed.";
        return false;
    }

    LoadRomInfo(core_->filePath, info_);
    BeginEdit();
    lastError_.clear();
    return true;
}
