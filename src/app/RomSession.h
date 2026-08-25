#pragma once

#include "RomInfo.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class SC4Core;
struct EventInfo;

struct EventUndoSnapshot {
    uint16_t match = 0;
    uint8_t type = 0;
    uint16_t xpos = 0;
    uint16_t ypos = 0;
    uint16_t eventId = 0;
    uint16_t eventSubId = 0;
    uint16_t spawnIndex = 0;
    uint16_t unknown = 0;
};

struct RomUndoSnapshot {
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram;
    std::vector<uint8_t> vram;
    std::vector<uint8_t> vramCache;
    std::vector<uint8_t> spriteCache;
    std::vector<uint16_t> mapping;
    std::vector<EventUndoSnapshot> events;
    std::vector<unsigned> spriteUpdate;
    std::vector<unsigned> simonSpriteUpdate;
    int selectedEventIndex = -1;
    uint64_t revision = 0;
};

class RomSession {
public:
    RomSession();
    ~RomSession();

    bool OpenRom(const std::string& path);
    bool LoadLevel(int level, int checkpoint = 0);
    bool LoadCurrentLayer(bool background);
    bool DeleteEvent(int& eventIndex);
    bool AddEvent(const EventInfo& event, int* eventIndex = nullptr);
    void SortEvents();
    void SlotEvents();
    void SaveEvents();
    bool ExpandRom();
    bool Save();
    bool SaveAs(const std::string& path);

    bool IsLoaded() const { return loaded_; }
    bool IsDirty() const { return dirty_; }
    const RomInfo& Info() const { return info_; }
    SC4Core& Core();
    const SC4Core& Core() const;
    int LevelCount() const { return levelCount_; }
    int LevelWidth() const;
    int LevelHeight() const;
    int CurrentLevel() const;
    int Region() const;
    bool IsExpandedRom() const;
    void BeginEdit();
    std::vector<uint8_t> CurrentRomBytes();
    unsigned ReadRom(unsigned snesAddress, int byteCount) const;
    void WriteRom(unsigned snesAddress, int byteCount, unsigned value);
    void WriteRomPc(unsigned pcOffset, int byteCount, unsigned value);
    void WriteRomAll(const std::vector<unsigned>& snesAddresses, int byteCount, unsigned value);
    RomUndoSnapshot CreateUndoSnapshot(int selectedEventIndex) const;
    void RestoreUndoSnapshot(const RomUndoSnapshot& snapshot, int& selectedEventIndex);
    const std::string& LastError() const { return lastError_; }

private:
    std::unique_ptr<SC4Core> core_;
    RomInfo info_;
    bool loaded_ = false;
    bool dirty_ = false;
    uint64_t revision_ = 0;
    uint64_t savedRevision_ = 0;
    uint64_t nextRevision_ = 1;
    int levelCount_ = 0;
    std::string lastError_;
};
