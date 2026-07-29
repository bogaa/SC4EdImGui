#include "PropertyPanel.h"

#include "EditorUndo.h"
#include "EventNames.h"
#include "imgui.h"
#include "SC4Core.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <list>
#include <set>
#include <string>
#include <vector>

namespace {

struct PropertyUiState {
    int whip = 0;
    int subweapon = 0;
    int movement = 0;
    int checkpoint = 0;
    int cameraLock = 0;
    int nextLevelDirection = 0;
    int exitCheck = 0;
    int exitType = 0;
    int enemyToAdd = 0x07;
    std::string enemyAddStatus;
};

static PropertyUiState g_propertyState;

static constexpr unsigned SUBWEAPON_DAMAGE_BASE = 0x81A6F8;
static constexpr unsigned TRIPLE_SHOT_PICKUP_JML = 0x80DFA3;
static constexpr unsigned AXE_STATE01_HOOK = 0x80BB05;
static constexpr unsigned KNIFE_STATE_POINTER = 0x80BA50;
static constexpr unsigned KNIFE_STATE_JML_STUB = 0x80FEDB;
static constexpr unsigned CLEAR_SELECTED_EVENT_SLOT_ALL = 0x808C59;
static constexpr unsigned LUNCH_SFX_FROM_ACCUM = 0x8085E3;
static constexpr unsigned READ_COLLISION_TABLE_7E4000 = 0x80CF86;
static constexpr unsigned MAKE_THIS_ENTITY_PLATFORM = 0x82C312;
static constexpr unsigned AXE_COUNTER = 0x80BB3A;
static constexpr unsigned AXE_ANIMATION = 0x80BB44;
static constexpr unsigned AXE_SPEED_MOVEMENT = 0x80BB11;
static constexpr unsigned CRUMBLE_BLOCK_BURN = 0x8290D1;

static float ValueColumnWidth()
{
    const float available = ImGui::GetContentRegionAvail().x;
    return available < 270.0f ? 104.0f : 128.0f;
}

static float LabelColumnWidth(float valueWidth)
{
    const float available = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return available > valueWidth + spacing ? available - valueWidth - spacing : available * 0.55f;
}

static void BeginPropertyRow(const char* label, float valueWidth)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetCursorPosX() + LabelColumnWidth(valueWidth));
    ImGui::SetNextItemWidth(valueWidth);
}

static void EndPropertyRow()
{
    ImGui::PopID();
}

static bool ComboRow(const char* label, int& value, const std::vector<std::string>& items)
{
    const char* preview = items.empty() ? "" : items[static_cast<size_t>(value)].c_str();
    bool changed = false;
    const float valueWidth = ValueColumnWidth();
    BeginPropertyRow(label, valueWidth);
    if (ImGui::BeginCombo("##value", preview)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const bool selected = value == i;
            if (ImGui::Selectable(items[static_cast<size_t>(i)].c_str(), selected)) {
                value = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    EndPropertyRow();
    return changed;
}

static std::vector<std::string> NumberItems(int count)
{
    std::vector<std::string> items;
    items.reserve(static_cast<size_t>(count));
    char text[16] = {};
    for (int i = 0; i < count; ++i) {
        std::snprintf(text, sizeof(text), "%d", i);
        items.emplace_back(text);
    }
    return items;
}

static void DrawNumberProperty(EditorState& state, const char* label, int byteCount, const std::vector<unsigned>& addresses, bool enabled = true)
{
    RomSession& session = state.session;
    const bool canEdit = session.IsLoaded() && enabled && !addresses.empty() && addresses.front() != 0;
    int value = canEdit ? static_cast<int>(session.ReadRom(addresses.front(), byteCount)) : 0;

    ImGui::BeginDisabled(!canEdit);
    const float valueWidth = byteCount == 1 ? 64.0f : byteCount == 2 ? 88.0f : 112.0f;
    BeginPropertyRow(label, valueWidth);
    if (ImGui::InputInt("##value", &value, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        const unsigned mask = byteCount == 1 ? 0xFFu : byteCount == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        session.WriteRomAll(addresses, byteCount, static_cast<unsigned>(value) & mask);
        state.levelRenderer.Invalidate();
    }
    if (ImGui::IsItemHovered() && !addresses.empty()) {
        if (addresses.size() == 1) {
            ImGui::SetTooltip("ROM address: %06X", addresses.front());
        } else {
            ImGui::SetTooltip("Writes %zu mirrored ROM addresses", addresses.size());
        }
    }
    EndPropertyRow();
    ImGui::EndDisabled();
}

static void DrawFlaggedWordProperty(EditorState& state, const char* label, const char* flagLabel, const std::vector<unsigned>& addresses, unsigned flagMask, bool enabled = true)
{
    RomSession& session = state.session;
    const bool canEdit = session.IsLoaded() && enabled && !addresses.empty() && addresses.front() != 0;
    const unsigned raw = canEdit ? session.ReadRom(addresses.front(), 2) : 0;
    int value = static_cast<int>(raw & ~flagMask & 0xFFFFu);
    bool flag = (raw & flagMask) != 0;

    ImGui::BeginDisabled(!canEdit);
    BeginPropertyRow(label, 88.0f);
    bool changed = false;
    if (ImGui::InputInt("##value", &value, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        changed = true;
    }
    if (ImGui::IsItemHovered() && !addresses.empty()) {
        ImGui::SetTooltip("ROM address: %06X, raw: %u", addresses.front(), raw & 0xFFFFu);
    }
    EndPropertyRow();

    BeginPropertyRow(flagLabel, 88.0f);
    if (ImGui::Checkbox("##flag", &flag)) {
        changed = true;
    }
    EndPropertyRow();

    if (changed) {
        const unsigned lowMask = (~flagMask) & 0xFFFFu;
        const unsigned newValue = (static_cast<unsigned>(value) & lowMask) | (flag ? flagMask : 0u);
        session.WriteRomAll(addresses, 2, newValue);
        state.levelRenderer.Invalidate();
    }
    ImGui::EndDisabled();
}

static void DrawInvertedNumberProperty(EditorState& state, const char* label, const std::vector<unsigned>& addresses, bool enabled = true)
{
    RomSession& session = state.session;
    const bool canEdit = session.IsLoaded() && enabled && !addresses.empty() && addresses.front() != 0;
    int value = canEdit ? static_cast<int>((0xFFFFu - session.ReadRom(addresses.front(), 2)) & 0xFFFFu) : 0;

    ImGui::BeginDisabled(!canEdit);
    BeginPropertyRow(label, 88.0f);
    if (ImGui::InputInt("##value", &value, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        session.WriteRomAll(addresses, 2, (0xFFFFu - static_cast<unsigned>(value)) & 0xFFFFu);
        state.levelRenderer.Invalidate();
    }
    if (ImGui::IsItemHovered() && !addresses.empty()) {
        if (addresses.size() == 1) {
            ImGui::SetTooltip("ROM address: %06X, stored as FFFF - value", addresses.front());
        } else {
            ImGui::SetTooltip("Writes %zu mirrored ROM addresses as FFFF - value", addresses.size());
        }
    }
    EndPropertyRow();
    ImGui::EndDisabled();
}

struct MovementProperty {
    const char* name;
    std::vector<unsigned> pixelAddresses;
    unsigned subpixelAddress;
    bool inverted;
    bool hasPixels;
};

static bool DrawEventNumberField(const char* label, unsigned& value, int byteCount)
{
    int editValue = static_cast<int>(value);
    const float valueWidth = byteCount == 1 ? 64.0f : 88.0f;
    BeginPropertyRow(label, valueWidth);
    const bool changed = ImGui::InputInt("##value", &editValue, 1, 10, ImGuiInputTextFlags_AutoSelectAll);
    if (changed) {
        const unsigned mask = byteCount == 1 ? 0xFFu : 0xFFFFu;
        value = static_cast<unsigned>(editValue) & mask;
    }
    EndPropertyRow();
    return changed;
}

static EventInfo* SelectedEvent(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        return nullptr;
    }

    SC4Core::EventList& events = state.session.Core().eventTable;
    if (state.selectedEventIndex < 0 || state.selectedEventIndex >= static_cast<int>(events.size())) {
        state.selectedEventIndex = -1;
        return nullptr;
    }

    auto iter = events.begin();
    std::advance(iter, state.selectedEventIndex);
    return &*iter;
}

static int FindMatchingEventIndex(const SC4Core::EventList& events, const EventInfo& target)
{
    int index = 0;
    int foundIndex = -1;
    for (const EventInfo& event : events) {
        if (event.match == target.match
            && event.type == target.type
            && event.xpos == target.xpos
            && event.ypos == target.ypos
            && event.eventId == target.eventId
            && event.eventSubId == target.eventSubId
            && event.spawnIndex == target.spawnIndex
            && event.unknown == target.unknown) {
            foundIndex = index;
        }
        ++index;
    }
    return foundIndex;
}

static void DrawComboProperty(EditorState& state, const char* label, int& value, const std::vector<std::string>& items, int byteCount, const std::vector<unsigned>& addresses, bool enabled = true)
{
    RomSession& session = state.session;
    const bool canEdit = session.IsLoaded() && enabled && !addresses.empty() && addresses.front() != 0;
    if (canEdit) {
        value = static_cast<int>(session.ReadRom(addresses.front(), byteCount));
        if (value < 0 || value >= static_cast<int>(items.size())) {
            value = 0;
        }
    }

    ImGui::BeginDisabled(!canEdit);
    if (ComboRow(label, value, items)) {
        session.WriteRomAll(addresses, byteCount, static_cast<unsigned>(value));
        state.levelRenderer.Invalidate();
    }
    ImGui::EndDisabled();
}

static unsigned LevelAddress(unsigned base, const EditorState& state, unsigned stride = 1)
{
    return base + static_cast<unsigned>(state.level) * stride;
}

static const char* EnemyName(SC4Core& core, unsigned id)
{
    EventInfo event = {};
    event.type = EVENT_TYPE_ENEMY;
    event.eventId = static_cast<WORD>(id);
    return EventDisplayName(core, event);
}

static bool EnemyIdAlreadyListed(EditorState& state, unsigned enemyListAddress, unsigned enemyCount, unsigned id)
{
    for (unsigned i = 0; i < enemyCount; ++i) {
        if (state.session.ReadRom(enemyListAddress + 1 + i, 1) == (id & 0xFFu)) {
            return true;
        }
    }
    return false;
}

static unsigned FindFreeBank86Address(EditorState& state, unsigned bytesNeeded)
{
    SC4Core& core = state.session.Core();
    if (!core.rom || bytesNeeded == 0) {
        return 0;
    }

    const unsigned bankStart = SNESCore::snes2pc(0x860000);
    const unsigned bankEnd = SNESCore::snes2pc(0x870000);
    if (bankStart >= core.romSize || bankEnd > core.romSize || bankEnd <= bankStart || bytesNeeded > bankEnd - bankStart) {
        return 0;
    }

    for (unsigned pc = bankStart; pc + bytesNeeded <= bankEnd; ++pc) {
        bool freeRun = true;
        for (unsigned i = 0; i < bytesNeeded; ++i) {
            if (core.rom[pc + i] != 0xFF) {
                freeRun = false;
                pc += i;
                break;
            }
        }
        if (freeRun) {
            return SNESCore::pc2snes(pc);
        }
    }

    return 0;
}

static bool AddEnemyToCurrentSet(EditorState& state, unsigned enemyListAddress, unsigned enemyCount, unsigned id)
{
    const unsigned gfxPointerTableAddress = 0x868B45 + static_cast<unsigned>(state.level) * 2u;
    const unsigned gfxSetOffset = state.session.ReadRom(gfxPointerTableAddress, 2);
    const unsigned gfxListAddress = 0x860000 + gfxSetOffset;
    const unsigned gfxMode = state.session.ReadRom(gfxListAddress, 2);
    if (gfxMode != 0) {
        return false;
    }

    unsigned gfxEntryCount = 0;
    unsigned gfxReadAddress = gfxListAddress + 2;
    while (state.session.ReadRom(gfxReadAddress, 2) != 0xFFFF) {
        ++gfxEntryCount;
        gfxReadAddress += 5;
        if (gfxEntryCount > 0x40) {
            return false;
        }
    }
    const unsigned oldGfxListBytes = 2 + gfxEntryCount * 5 + 2;
    const unsigned newGfxListBytes = oldGfxListBytes + 5;

    unsigned slotNum = 0;
    for (unsigned i = 0; i < enemyCount; ++i) {
        const unsigned index = state.session.ReadRom(enemyListAddress + 1 + i, 1);
        slotNum += state.session.ReadRom(0x81AA80 + index, 1);
    }

    const unsigned spriteCount = state.session.ReadRom(0x81AA80 + (id & 0x7Fu), 1);
    const unsigned spriteDest = 0x6A00 + slotNum * 0x200;
    if (spriteCount == 0 || spriteDest + spriteCount * 0x200 > 0x8000) {
        return false;
    }

    const unsigned sourceLow = state.session.ReadRom(0x81A900 + (id & 0x7Fu) * 3u, 2);
    const unsigned sourceBank = state.session.ReadRom(0x81A900 + (id & 0x7Fu) * 3u + 2u, 1);
    const unsigned sourceAddress = sourceLow | (sourceBank << 16);
    if ((sourceAddress >> 16) == 0 || sourceLow == 0) {
        return false;
    }

    const unsigned newCount = enemyCount + 1;
    const unsigned newIdListBytes = newCount + 1;
    const unsigned newDataAddress = FindFreeBank86Address(state, newIdListBytes + newGfxListBytes);
    if ((newDataAddress >> 16) != 0x86) {
        return false;
    }
    const unsigned newListAddress = newDataAddress;
    const unsigned newGfxListAddress = newDataAddress + newIdListBytes;

    PushUndo(state);
    state.session.WriteRom(newListAddress, 1, newCount);
    for (unsigned i = 0; i < enemyCount; ++i) {
        state.session.WriteRom(newListAddress + 1 + i, 1, state.session.ReadRom(enemyListAddress + 1 + i, 1));
    }
    state.session.WriteRom(newListAddress + 1 + enemyCount, 1, id & 0xFFu);
    state.session.WriteRom(0x868BCD + static_cast<unsigned>(state.level) * 2u, 2, newListAddress & 0xFFFFu);

    for (unsigned i = 0; i < oldGfxListBytes - 2; ++i) {
        state.session.WriteRom(newGfxListAddress + i, 1, state.session.ReadRom(gfxListAddress + i, 1));
    }
    unsigned writeAddress = newGfxListAddress + oldGfxListBytes - 2;
    state.session.WriteRom(writeAddress, 2, spriteDest);
    state.session.WriteRom(writeAddress + 2, 2, sourceLow);
    state.session.WriteRom(writeAddress + 4, 1, sourceBank);
    state.session.WriteRom(writeAddress + 5, 2, 0xFFFF);
    state.session.WriteRom(gfxPointerTableAddress, 2, newGfxListAddress & 0xFFFFu);

    state.session.LoadCurrentLayer(state.showBackground);
    state.levelRenderer.Invalidate();
    return true;
}

static bool RemoveEnemyFromCurrentSet(EditorState& state, unsigned enemyListAddress, unsigned enemyCount, unsigned removeIndex)
{
    if (enemyCount == 0 || removeIndex >= enemyCount) {
        return false;
    }

    const unsigned gfxSetOffset = state.session.ReadRom(0x868B45 + static_cast<unsigned>(state.level) * 2u, 2);
    const unsigned gfxListAddress = 0x860000 + gfxSetOffset;
    if (state.session.ReadRom(gfxListAddress, 2) != 0) {
        return false;
    }

    unsigned gfxEntryCount = 0;
    unsigned gfxReadAddress = gfxListAddress + 2;
    while (state.session.ReadRom(gfxReadAddress, 2) != 0xFFFF) {
        ++gfxEntryCount;
        gfxReadAddress += 5;
        if (gfxEntryCount > 0x40) {
            return false;
        }
    }
    if (removeIndex >= gfxEntryCount) {
        return false;
    }

    PushUndo(state);
    for (unsigned i = removeIndex; i + 1 < enemyCount; ++i) {
        state.session.WriteRom(enemyListAddress + 1 + i, 1, state.session.ReadRom(enemyListAddress + 2 + i, 1));
    }
    state.session.WriteRom(enemyListAddress, 1, enemyCount - 1);
    state.session.WriteRom(enemyListAddress + enemyCount, 1, 0xFF);

    const unsigned removedGfxAddress = gfxListAddress + 2 + removeIndex * 5;
    for (unsigned i = removeIndex; i + 1 < gfxEntryCount; ++i) {
        const unsigned src = gfxListAddress + 2 + (i + 1) * 5;
        const unsigned dst = gfxListAddress + 2 + i * 5;
        for (unsigned byte = 0; byte < 5; ++byte) {
            state.session.WriteRom(dst + byte, 1, state.session.ReadRom(src + byte, 1));
        }
    }
    const unsigned terminatorAddress = gfxListAddress + 2 + (gfxEntryCount - 1) * 5;
    state.session.WriteRom(terminatorAddress, 2, 0xFFFF);
    for (unsigned byte = 2; byte < 5; ++byte) {
        state.session.WriteRom(terminatorAddress + byte, 1, 0xFF);
    }
    (void)removedGfxAddress;

    state.session.LoadCurrentLayer(state.showBackground);
    state.levelRenderer.Invalidate();
    return true;
}

static void DrawCurrentLevelEnemies(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        return;
    }

    SC4Core& core = state.session.Core();
    const unsigned pointerTableAddress = LevelAddress(0x868BCD, state, 2);
    const unsigned enemySetOffset = state.session.ReadRom(pointerTableAddress, 2);
    const unsigned enemyListAddress = 0x860000 + enemySetOffset;
    unsigned enemyCount = state.session.ReadRom(enemyListAddress, 1);
    if (enemyCount > 0x40) {
        enemyCount = 0x40;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Enemies");
    ImGui::TextDisabled("Set ID pointer: $%06X -> $%06X", pointerTableAddress, enemyListAddress);

    if (enemyCount == 0) {
        ImGui::TextDisabled("No enemy IDs in this set.");
    } else if (ImGui::BeginTable("current-level-enemies", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("Enemy");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        for (unsigned i = 0; i < enemyCount; ++i) {
            const unsigned id = state.session.ReadRom(enemyListAddress + 1 + i, 1);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("$%02X", id & 0xFF);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(EnemyName(core, id));
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("Remove")) {
                g_propertyState.enemyAddStatus = RemoveEnemyFromCurrentSet(state, enemyListAddress, enemyCount, i)
                    ? "Removed enemy availability and graphics load entry."
                    : "Could not remove enemy from this packed list.";
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    std::set<WORD> compatibleIds;
    core.GetActiveEnemyId(compatibleIds);
    ImGui::TextDisabled("%u listed, %d compatible via shared graphics", enemyCount, static_cast<int>(compatibleIds.size()));

    g_propertyState.enemyToAdd &= 0x7F;
    const unsigned addId = static_cast<unsigned>(g_propertyState.enemyToAdd);
    ImGui::SetNextItemWidth(72.0f);
    ImGui::InputInt("Enemy ID", &g_propertyState.enemyToAdd, 1, 16, ImGuiInputTextFlags_AutoSelectAll);
    g_propertyState.enemyToAdd &= 0x7F;
    const bool alreadyListed = EnemyIdAlreadyListed(state, enemyListAddress, enemyCount, addId);
    const bool compatible = compatibleIds.count(static_cast<WORD>(addId)) != 0;
    ImGui::BeginDisabled(alreadyListed);
    if (ImGui::Button("Add Available Enemy")) {
        g_propertyState.enemyAddStatus = AddEnemyToCurrentSet(state, enemyListAddress, enemyCount, addId)
            ? "Added enemy availability and graphics load entry."
            : "Could not add: no free bank $86 space, invalid graphics pointer, or no sprite VRAM slot room.";
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s%s",
        EnemyName(core, addId),
        compatible ? " - compatible graphics" : " - new graphics entry will be added");
    if (alreadyListed) {
        ImGui::TextDisabled("This ID is in the current graphics assignment list.");
    } else {
        ImGui::TextDisabled("Copies and repoints both enemy ID and enemy graphics lists for this level.");
    }
    if (!g_propertyState.enemyAddStatus.empty()) {
        ImGui::TextDisabled("%s", g_propertyState.enemyAddStatus.c_str());
    }
}

static void DrawGeneralProperties(EditorState& state)
{
    if (ImGui::CollapsingHeader("Global", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawNumberProperty(state, "Lives", 2, { 0x8094DB });
        DrawNumberProperty(state, "Continue lives", 2, { 0x8CFD9B });
        DrawNumberProperty(state, "Timer", 2, { LevelAddress(0x85BCF8, state, 2) });
    }
}

struct KnifePickupModeLocation {
    unsigned address = 0;
    unsigned value = 0;
    int byteCount = 0;
};

static bool CanReadRomPc(const SC4Core& core, unsigned pcOffset, unsigned byteCount)
{
    return core.rom && pcOffset <= core.romSize && byteCount <= core.romSize - pcOffset;
}

static bool TrySnesToPc(const SC4Core& core, unsigned snesAddress, unsigned byteCount, unsigned& pcOffset)
{
    if (snesAddress == 0 || !core.rom) {
        return false;
    }
    pcOffset = SNESCore::snes2pc(static_cast<int>(snesAddress));
    return CanReadRomPc(core, pcOffset, byteCount);
}

static unsigned ReadRomLong24(EditorState& state, unsigned address)
{
    return state.session.ReadRom(address, 2) | (state.session.ReadRom(address + 2, 1) << 16);
}

static void EmitByte(std::vector<unsigned char>& code, unsigned value)
{
    code.push_back(static_cast<unsigned char>(value & 0xFFu));
}

static void EmitWord(std::vector<unsigned char>& code, unsigned value)
{
    EmitByte(code, value);
    EmitByte(code, value >> 8);
}

static void EmitLong24(std::vector<unsigned char>& code, unsigned value)
{
    EmitByte(code, value);
    EmitByte(code, value >> 8);
    EmitByte(code, value >> 16);
}

static size_t EmitBranch8(std::vector<unsigned char>& code, unsigned opcode)
{
    EmitByte(code, opcode);
    const size_t operandOffset = code.size();
    EmitByte(code, 0);
    return operandOffset;
}

static void PatchBranch8(std::vector<unsigned char>& code, size_t operandOffset, size_t targetOffset)
{
    const int relative = static_cast<int>(targetOffset) - static_cast<int>(operandOffset + 1);
    code[operandOffset] = static_cast<unsigned char>(relative & 0xFF);
}

static size_t EmitBranch16(std::vector<unsigned char>& code)
{
    EmitByte(code, 0x82);
    const size_t operandOffset = code.size();
    EmitWord(code, 0);
    return operandOffset;
}

static void PatchBranch16(std::vector<unsigned char>& code, size_t operandOffset, size_t targetOffset)
{
    const int relative = static_cast<int>(targetOffset) - static_cast<int>(operandOffset + 2);
    code[operandOffset] = static_cast<unsigned char>(relative & 0xFF);
    code[operandOffset + 1] = static_cast<unsigned char>((relative >> 8) & 0xFF);
}

static std::vector<unsigned char> BuildKnifePlatformRoutine(unsigned routineAddress)
{
    std::vector<unsigned char> code;

    EmitByte(code, 0x08);                                    // PHP
    EmitByte(code, 0xC2); EmitByte(code, 0x30);              // REP #$30
    EmitByte(code, 0xA5); EmitByte(code, 0x90);              // LDA $90
    size_t hasMode = EmitBranch8(code, 0xD0);                // BNE hasMode
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x6B);                                    // RTL
    PatchBranch8(code, hasMode, code.size());

    EmitByte(code, 0xC9); EmitWord(code, 0x0003);            // CMP #$0003
    size_t isPlatform = EmitBranch8(code, 0xF0);             // BEQ platform
    EmitByte(code, 0xC9); EmitWord(code, 0x0002);            // CMP #$0002
    size_t isKey = EmitBranch8(code, 0xF0);                  // BEQ key
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x6B);                                    // RTL
    PatchBranch8(code, isKey, code.size());
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x6B);                                    // key: RTL

    PatchBranch8(code, isPlatform, code.size());
    EmitByte(code, 0xB5); EmitByte(code, 0x1A);              // LDA $1A,x
    size_t noMovement = EmitBranch8(code, 0xF0);             // BEQ noMovement
    EmitByte(code, 0xDA);                                    // PHX
    EmitByte(code, 0x22); EmitLong24(code, 0);               // JSL collision helper (patched below)
    const size_t collisionHelperOperand = code.size() - 3;
    size_t noWall = EmitBranch8(code, 0xF0);                 // BEQ noWall
    EmitByte(code, 0xFA);                                    // PLX
    EmitByte(code, 0x74); EmitByte(code, 0x1A);              // STZ $1A,x
    EmitByte(code, 0xDA);                                    // PHX
    PatchBranch8(code, noWall, code.size());
    EmitByte(code, 0xFA);                                    // PLX
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x6B);                                    // RTL

    PatchBranch8(code, noMovement, code.size());
    EmitByte(code, 0xF6); EmitByte(code, 0x20);              // INC $20,x
    EmitByte(code, 0xB5); EmitByte(code, 0x20);              // LDA $20,x
    EmitByte(code, 0xC9); EmitWord(code, 0x0080);            // CMP #$0080
    size_t skipBlink = EmitBranch8(code, 0x90);              // BCC skipBlink
    EmitByte(code, 0xB5); EmitByte(code, 0x00);              // LDA $00,x
    size_t restoreBlink = EmitBranch8(code, 0xF0);           // BEQ restoreBlink
    EmitByte(code, 0x95); EmitByte(code, 0x22);              // STA $22,x
    EmitByte(code, 0xA5); EmitByte(code, 0x3A);              // LDA $3A
    EmitByte(code, 0x89); EmitWord(code, 0x0010);            // BIT #$0010
    size_t restoreBlink2 = EmitBranch8(code, 0xF0);          // BEQ restoreBlink
    EmitByte(code, 0x74); EmitByte(code, 0x00);              // STZ $00,x
    size_t afterBlink = EmitBranch8(code, 0x80);             // BRA afterBlink
    PatchBranch8(code, restoreBlink, code.size());
    PatchBranch8(code, restoreBlink2, code.size());
    EmitByte(code, 0xB5); EmitByte(code, 0x22);              // LDA $22,x
    EmitByte(code, 0x95); EmitByte(code, 0x00);              // STA $00,x
    PatchBranch8(code, afterBlink, code.size());

    PatchBranch8(code, skipBlink, code.size());
    EmitByte(code, 0xB5); EmitByte(code, 0x20);              // LDA $20,x
    EmitByte(code, 0xC9); EmitWord(code, 0x0100);            // CMP #$0100
    size_t makePlatform = EmitBranch8(code, 0x90);           // BCC makePlatform
    EmitByte(code, 0xEC); EmitWord(code, 0x13C8);            // CPX $13C8
    size_t notTracked = EmitBranch8(code, 0xD0);             // BNE notTracked
    EmitByte(code, 0x64); EmitByte(code, 0x80);              // STZ $80
    PatchBranch8(code, notTracked, code.size());
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x5C); EmitLong24(code, CLEAR_SELECTED_EVENT_SLOT_ALL);

    PatchBranch8(code, makePlatform, code.size());
    EmitByte(code, 0x22); EmitLong24(code, MAKE_THIS_ENTITY_PLATFORM);
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x6B);                                    // RTL

    const unsigned collisionHelperAddress = routineAddress + static_cast<unsigned>(code.size());
    code[collisionHelperOperand + 0] = static_cast<unsigned char>(collisionHelperAddress & 0xFFu);
    code[collisionHelperOperand + 1] = static_cast<unsigned char>((collisionHelperAddress >> 8) & 0xFFu);
    code[collisionHelperOperand + 2] = static_cast<unsigned char>((collisionHelperAddress >> 16) & 0xFFu);

    EmitByte(code, 0xB5); EmitByte(code, 0x0E);              // LDA $0E,x
    EmitByte(code, 0x85); EmitByte(code, 0x02);              // STA $02
    EmitByte(code, 0xB5); EmitByte(code, 0x1A);              // LDA $1A,x
    size_t positiveSpeed = EmitBranch8(code, 0x10);          // BPL positiveSpeed
    EmitByte(code, 0xB5); EmitByte(code, 0x0A);              // LDA $0A,x
    EmitByte(code, 0x38);                                    // SEC
    EmitByte(code, 0xE9); EmitWord(code, 0x000C);            // SBC #$000C
    size_t storeCollisionX = EmitBranch8(code, 0x80);        // BRA storeCollisionX
    PatchBranch8(code, positiveSpeed, code.size());
    EmitByte(code, 0xA9); EmitWord(code, 0x000C);            // LDA #$000C
    EmitByte(code, 0x18);                                    // CLC
    EmitByte(code, 0x75); EmitByte(code, 0x0A);              // ADC $0A,x
    PatchBranch8(code, storeCollisionX, code.size());
    EmitByte(code, 0x85); EmitByte(code, 0x00);              // STA $00
    EmitByte(code, 0x5C); EmitLong24(code, READ_COLLISION_TABLE_7E4000);

    return code;
}

static std::vector<unsigned char> BuildTriplePickupRoutine()
{
    std::vector<unsigned char> code;
    EmitByte(code, 0xA9); EmitWord(code, 0x0003);            // LDA #$0003
    EmitByte(code, 0x85); EmitByte(code, 0x90);              // STA $90
    EmitByte(code, 0xA9); EmitWord(code, 0x0084);            // LDA #$0084
    EmitByte(code, 0x22); EmitLong24(code, LUNCH_SFX_FROM_ACCUM);
    EmitByte(code, 0x5C); EmitLong24(code, CLEAR_SELECTED_EVENT_SLOT_ALL);
    return code;
}

static std::vector<unsigned char> BuildKnifePickupRoutine()
{
    return BuildTriplePickupRoutine();
}

static std::vector<unsigned char> BuildAxeBlockBreakerRoutine()
{
    std::vector<unsigned char> code;

    EmitByte(code, 0x08);                                    // PHP
    EmitByte(code, 0xC2); EmitByte(code, 0x30);              // REP #$30
    EmitByte(code, 0xA5); EmitByte(code, 0x90);              // LDA $90
    EmitByte(code, 0xC9); EmitWord(code, 0x0003);            // CMP #$0003
    size_t specialMode = EmitBranch8(code, 0xF0);            // BEQ specialMode

    const size_t normalMode = code.size();
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x22); EmitLong24(code, AXE_COUNTER);
    EmitByte(code, 0x22); EmitLong24(code, AXE_ANIMATION);
    EmitByte(code, 0x5C); EmitLong24(code, AXE_SPEED_MOVEMENT);

    PatchBranch8(code, specialMode, code.size());
    EmitByte(code, 0xA0); EmitWord(code, 0x0400);            // LDY #$0400

    const size_t loop = code.size();
    EmitByte(code, 0xC0); EmitWord(code, 0x0F00);            // CPY #$0F00
    size_t keepScanning = EmitBranch8(code, 0x90);           // BCC keepScanning
    size_t noHit = EmitBranch16(code);                       // BRL noHit
    PatchBranch8(code, keepScanning, code.size());
    EmitByte(code, 0xC4); EmitByte(code, 0xFC);              // CPY $FC
    size_t nextSameSlot = EmitBranch8(code, 0xF0);           // BEQ nextSlot

    EmitByte(code, 0xB9); EmitWord(code, 0x0010);            // LDA $10,y
    EmitByte(code, 0xC9); EmitWord(code, 0x002F);            // CMP #$002F
    size_t possibleBlock2F = EmitBranch8(code, 0xF0);
    EmitByte(code, 0xC9); EmitWord(code, 0x0037);            // CMP #$0037
    size_t possibleBlock37 = EmitBranch8(code, 0xF0);
    EmitByte(code, 0xC9); EmitWord(code, 0x0064);            // CMP #$0064
    size_t possibleBlock64 = EmitBranch8(code, 0xF0);
    EmitByte(code, 0xC9); EmitWord(code, 0x0065);            // CMP #$0065
    size_t nextNotBlock = EmitBranch8(code, 0xD0);           // BNE nextSlot

    const size_t possibleBlock = code.size();
    PatchBranch8(code, possibleBlock2F, possibleBlock);
    PatchBranch8(code, possibleBlock37, possibleBlock);
    PatchBranch8(code, possibleBlock64, possibleBlock);

    EmitByte(code, 0xB9); EmitWord(code, 0x000A);            // LDA $0A,y
    EmitByte(code, 0x29); EmitWord(code, 0xFFF0);            // AND #$FFF0
    EmitByte(code, 0x85); EmitByte(code, 0x00);              // STA $00
    EmitByte(code, 0xB5); EmitByte(code, 0x0A);              // LDA $0A,x
    EmitByte(code, 0x29); EmitWord(code, 0xFFF0);            // AND #$FFF0
    EmitByte(code, 0xC5); EmitByte(code, 0x00);              // CMP $00
    size_t nextXMiss = EmitBranch8(code, 0xD0);              // BNE nextSlot

    EmitByte(code, 0xB9); EmitWord(code, 0x000E);            // LDA $0E,y
    EmitByte(code, 0x29); EmitWord(code, 0xFFF0);            // AND #$FFF0
    EmitByte(code, 0x85); EmitByte(code, 0x00);              // STA $00
    EmitByte(code, 0xB5); EmitByte(code, 0x0E);              // LDA $0E,x
    EmitByte(code, 0x29); EmitWord(code, 0xFFF0);            // AND #$FFF0
    EmitByte(code, 0xC5); EmitByte(code, 0x00);              // CMP $00
    size_t nextYMiss = EmitBranch8(code, 0xD0);              // BNE nextSlot

    EmitByte(code, 0xDA);                                    // PHX
    EmitByte(code, 0xBB);                                    // TYX
    EmitByte(code, 0x22); EmitLong24(code, CRUMBLE_BLOCK_BURN);
    EmitByte(code, 0xFA);                                    // PLX
    EmitByte(code, 0x28);                                    // PLP
    EmitByte(code, 0x5C); EmitLong24(code, CLEAR_SELECTED_EVENT_SLOT_ALL);

    const size_t nextSlot = code.size();
    PatchBranch8(code, nextSameSlot, nextSlot);
    PatchBranch8(code, nextNotBlock, nextSlot);
    PatchBranch8(code, nextXMiss, nextSlot);
    PatchBranch8(code, nextYMiss, nextSlot);
    EmitByte(code, 0x98);                                    // TYA
    EmitByte(code, 0x18);                                    // CLC
    EmitByte(code, 0x69); EmitWord(code, 0x0040);            // ADC #$0040
    EmitByte(code, 0xA8);                                    // TAY
    size_t backToLoop = EmitBranch16(code);                  // BRL loop
    PatchBranch16(code, backToLoop, loop);

    PatchBranch16(code, noHit, code.size());
    size_t toNormal = EmitBranch16(code);
    PatchBranch16(code, toNormal, normalMode);

    return code;
}

static unsigned FindFreeCodeAddress(EditorState& state, unsigned bytesNeeded)
{
    SC4Core& core = state.session.Core();
    if (!core.rom || bytesNeeded == 0 || bytesNeeded > core.romSize) {
        return 0;
    }

    const unsigned preferredStart = SNESCore::snes2pc(0x878000);
    const unsigned starts[] = { preferredStart < core.romSize ? preferredStart : 0u, SNESCore::snes2pc(0x868000), 0u };
    for (unsigned start : starts) {
        if (start >= core.romSize) {
            continue;
        }
        for (unsigned pc = start; pc + bytesNeeded <= core.romSize; ++pc) {
            const unsigned startAddress = SNESCore::pc2snes(static_cast<int>(pc));
            const unsigned endAddress = SNESCore::pc2snes(static_cast<int>(pc + bytesNeeded - 1));
            if ((startAddress & 0xFF0000u) != (endAddress & 0xFF0000u)) {
                pc = (pc + 0x8000u) & ~0x7FFFu;
                if (pc == 0) {
                    break;
                }
                --pc;
                continue;
            }

            bool freeRun = true;
            for (unsigned i = 0; i < bytesNeeded; ++i) {
                if (core.rom[pc + i] != 0xFF) {
                    freeRun = false;
                    pc += i;
                    break;
                }
            }
            if (freeRun) {
                return SNESCore::pc2snes(static_cast<int>(pc));
            }
        }
    }

    return 0;
}

static void WriteCode(EditorState& state, unsigned snesAddress, const std::vector<unsigned char>& code)
{
    const unsigned pc = SNESCore::snes2pc(static_cast<int>(snesAddress));
    for (size_t i = 0; i < code.size(); ++i) {
        state.session.WriteRomPc(pc + static_cast<unsigned>(i), 1, code[i]);
    }
}

static void WriteJml(EditorState& state, unsigned hookAddress, unsigned targetAddress)
{
    state.session.WriteRom(hookAddress, 1, 0x5C);
    state.session.WriteRom(hookAddress + 1, 1, targetAddress & 0xFFu);
    state.session.WriteRom(hookAddress + 2, 1, (targetAddress >> 8) & 0xFFu);
    state.session.WriteRom(hookAddress + 3, 1, (targetAddress >> 16) & 0xFFu);
}

static bool LooksLikeKnifePlatformRoutine(const SC4Core& core, unsigned snesAddress)
{
    unsigned pc = 0;
    return TrySnesToPc(core, snesAddress, 6, pc)
        && core.rom[pc + 0] == 0x08
        && core.rom[pc + 1] == 0xC2
        && core.rom[pc + 2] == 0x30
        && core.rom[pc + 3] == 0xA5
        && core.rom[pc + 4] == 0x90
        && core.rom[pc + 5] == 0xD0;
}

static bool TryFindKnifePlatformRoutine(EditorState& state, unsigned& routineAddress)
{
    routineAddress = 0;
    SC4Core& core = state.session.Core();
    unsigned stubPc = 0;
    if (TrySnesToPc(core, KNIFE_STATE_JML_STUB, 4, stubPc) && core.rom[stubPc] == 0x5C) {
        const unsigned target = ReadRomLong24(state, KNIFE_STATE_JML_STUB + 1);
        if (LooksLikeKnifePlatformRoutine(core, target)) {
            routineAddress = target;
            return true;
        }
    }

    if (!core.rom || core.romSize < 6) {
        return false;
    }
    for (unsigned pc = 0; pc + 6 < core.romSize; ++pc) {
        const unsigned address = SNESCore::pc2snes(static_cast<int>(pc));
        if (LooksLikeKnifePlatformRoutine(core, address)) {
            routineAddress = address;
            return true;
        }
    }
    return false;
}

static bool LooksLikeAxeBlockBreakerRoutine(const SC4Core& core, unsigned snesAddress)
{
    unsigned pc = 0;
    return TrySnesToPc(core, snesAddress, 8, pc)
        && core.rom[pc + 0] == 0x08
        && core.rom[pc + 1] == 0xC2
        && core.rom[pc + 2] == 0x30
        && core.rom[pc + 3] == 0xA5
        && core.rom[pc + 4] == 0x90
        && core.rom[pc + 5] == 0xC9
        && core.rom[pc + 6] == 0x03
        && core.rom[pc + 7] == 0x00;
}

static bool TryFindAxeBlockBreakerRoutine(EditorState& state, unsigned& routineAddress)
{
    routineAddress = 0;
    SC4Core& core = state.session.Core();
    unsigned hookPc = 0;
    if (TrySnesToPc(core, AXE_STATE01_HOOK, 4, hookPc) && core.rom[hookPc] == 0x5C) {
        const unsigned target = ReadRomLong24(state, AXE_STATE01_HOOK + 1);
        if (LooksLikeAxeBlockBreakerRoutine(core, target)) {
            routineAddress = target;
            return true;
        }
    }

    if (!core.rom || core.romSize < 8) {
        return false;
    }
    for (unsigned pc = 0; pc + 8 < core.romSize; ++pc) {
        const unsigned address = SNESCore::pc2snes(static_cast<int>(pc));
        if (LooksLikeAxeBlockBreakerRoutine(core, address)) {
            routineAddress = address;
            return true;
        }
    }
    return false;
}

static bool HasSimonPickupModeStoreNear(const SC4Core& core, unsigned startPc, unsigned endPc)
{
    endPc = endPc < core.romSize ? endPc : core.romSize;
    for (unsigned pc = startPc; pc + 1 < endPc; ++pc) {
        if (core.rom[pc] == 0x85 && core.rom[pc + 1] == 0x90) {
            return true;
        }
        if (pc + 2 < endPc && core.rom[pc] == 0x8D && core.rom[pc + 1] == 0x90 && core.rom[pc + 2] == 0x00) {
            return true;
        }
    }
    return false;
}

static bool TryKnifePickupModeAtPc(const SC4Core& core, unsigned pc, KnifePickupModeLocation& location)
{
    if (!CanReadRomPc(core, pc, 8) || core.rom[pc] != 0xA9) {
        return false;
    }

    const unsigned lowByteMode = core.rom[pc + 1];
    if ((lowByteMode == 2 || lowByteMode == 3) && HasSimonPickupModeStoreNear(core, pc + 2, pc + 12)) {
        location.address = SNESCore::pc2snes(static_cast<int>(pc + 1));
        location.value = lowByteMode;
        location.byteCount = 1;
        return true;
    }

    if (!CanReadRomPc(core, pc, 9) || core.rom[pc + 2] != 0x00) {
        return false;
    }
    const unsigned wordMode = core.rom[pc + 1] | (core.rom[pc + 2] << 8);
    if ((wordMode == 2 || wordMode == 3) && HasSimonPickupModeStoreNear(core, pc + 3, pc + 13)) {
        location.address = SNESCore::pc2snes(static_cast<int>(pc + 1));
        location.value = wordMode;
        location.byteCount = 2;
        return true;
    }

    return false;
}

static bool TryFindKnifePlatformPickupMode(EditorState& state, KnifePickupModeLocation& location)
{
    location = {};

    SC4Core& core = state.session.Core();
    unsigned hookPc = 0;
    if (TrySnesToPc(core, TRIPLE_SHOT_PICKUP_JML, 4, hookPc) && core.rom[hookPc] == 0x5C) {
        const unsigned target = ReadRomLong24(state, TRIPLE_SHOT_PICKUP_JML + 1);
        unsigned targetPc = 0;
        if (TrySnesToPc(core, target, 13, targetPc) && TryKnifePickupModeAtPc(core, targetPc, location)) {
            return true;
        }
    }

    if (!core.rom || core.romSize < 13) {
        return false;
    }

    for (unsigned pc = 0; pc + 13 < core.romSize; ++pc) {
        if (TryKnifePickupModeAtPc(core, pc, location)) {
            return true;
        }
    }

    return false;
}

static bool InstallTripleModePickup(EditorState& state, unsigned& pickupAddress)
{
    KnifePickupModeLocation pickupLocation;
    if (TryFindKnifePlatformPickupMode(state, pickupLocation)) {
        state.session.WriteRom(pickupLocation.address, pickupLocation.byteCount, 3);
        pickupAddress = pickupLocation.address - 1;
        return true;
    }

    const std::vector<unsigned char> pickupCode = BuildKnifePickupRoutine();
    pickupAddress = FindFreeCodeAddress(state, static_cast<unsigned>(pickupCode.size()));
    if (pickupAddress == 0) {
        return false;
    }
    WriteCode(state, pickupAddress, pickupCode);
    WriteJml(state, TRIPLE_SHOT_PICKUP_JML, pickupAddress);
    state.session.WriteRom(TRIPLE_SHOT_PICKUP_JML + 4, 1, 0xEA);
    state.session.WriteRom(TRIPLE_SHOT_PICKUP_JML + 5, 1, 0xEA);
    return true;
}

static bool InstallKnifePlatformPatch(EditorState& state, unsigned& routineAddress, unsigned& pickupAddress)
{
    routineAddress = 0;
    pickupAddress = 0;

    if (!TryFindKnifePlatformRoutine(state, routineAddress)) {
        const std::vector<unsigned char> sizeProbe = BuildKnifePlatformRoutine(0);
        routineAddress = FindFreeCodeAddress(state, static_cast<unsigned>(sizeProbe.size()));
        if (routineAddress == 0) {
            return false;
        }
        WriteCode(state, routineAddress, BuildKnifePlatformRoutine(routineAddress));
    }

    WriteJml(state, KNIFE_STATE_JML_STUB, routineAddress);
    state.session.WriteRom(KNIFE_STATE_POINTER, 2, KNIFE_STATE_JML_STUB & 0xFFFFu);

    if (!InstallTripleModePickup(state, pickupAddress)) {
        return false;
    }

    return true;
}

static bool InstallAxeBlockBreakerPatch(EditorState& state, unsigned& routineAddress, unsigned& pickupAddress)
{
    routineAddress = 0;
    pickupAddress = 0;

    if (!TryFindAxeBlockBreakerRoutine(state, routineAddress)) {
        const std::vector<unsigned char> routine = BuildAxeBlockBreakerRoutine();
        routineAddress = FindFreeCodeAddress(state, static_cast<unsigned>(routine.size()));
        if (routineAddress == 0) {
            return false;
        }
        WriteCode(state, routineAddress, routine);
    }

    WriteJml(state, AXE_STATE01_HOOK, routineAddress);
    if (!InstallTripleModePickup(state, pickupAddress)) {
        return false;
    }

    return true;
}

static void DrawKnifePlatformPickupProperty(EditorState& state)
{
    KnifePickupModeLocation modeLocation;
    const bool hasPickupMode = TryFindKnifePlatformPickupMode(state, modeLocation);
    unsigned routineAddress = 0;
    const bool hasRuntime = TryFindKnifePlatformRoutine(state, routineAddress);
    bool enabled = hasRuntime && hasPickupMode && modeLocation.value == 3;

    if (ImGui::Checkbox("Knife platform pickup", &enabled)) {
        PushUndo(state);
        if (enabled) {
            unsigned installedRoutine = 0;
            unsigned installedPickup = 0;
            if (InstallKnifePlatformPatch(state, installedRoutine, installedPickup)) {
                routineAddress = installedRoutine;
                modeLocation.address = installedPickup + 1;
                modeLocation.byteCount = 2;
                modeLocation.value = 3;
            }
        } else if (hasPickupMode) {
            state.session.WriteRom(modeLocation.address, modeLocation.byteCount, 2);
            modeLocation.value = 2;
        }
        state.levelRenderer.Invalidate();
    }

    if (hasRuntime && hasPickupMode) {
        ImGui::TextDisabled("Installed at $%06X. Pickup mode value at $%06X.", routineAddress, modeLocation.address);
    } else if (hasRuntime) {
        ImGui::TextDisabled("Runtime found at $%06X. Checking the box will install the pickup hook.", routineAddress);
    } else if (hasPickupMode) {
        ImGui::TextDisabled("Pickup mode found at $%06X. Checking the box will install the knife runtime hook.", modeLocation.address);
    } else {
        ImGui::TextDisabled("Checking this writes the knife runtime and pickup hook into free ROM space.");
    }
}

static void DrawAxeBlockBreakerProperty(EditorState& state)
{
    KnifePickupModeLocation modeLocation;
    const bool hasPickupMode = TryFindKnifePlatformPickupMode(state, modeLocation);
    unsigned routineAddress = 0;
    const bool hasRuntime = TryFindAxeBlockBreakerRoutine(state, routineAddress);
    bool enabled = hasRuntime && hasPickupMode && modeLocation.value == 3;

    if (ImGui::Checkbox("Axe breaks blocks", &enabled)) {
        PushUndo(state);
        if (enabled) {
            unsigned installedRoutine = 0;
            unsigned installedPickup = 0;
            if (InstallAxeBlockBreakerPatch(state, installedRoutine, installedPickup)) {
                routineAddress = installedRoutine;
            }
        } else if (hasPickupMode) {
            state.session.WriteRom(modeLocation.address, modeLocation.byteCount, 2);
        }
        state.levelRenderer.Invalidate();
    }

    if (hasRuntime && hasPickupMode) {
        ImGui::TextDisabled("Installed at $%06X. Axe + Triple mode breaks block events.", routineAddress);
    } else if (hasRuntime) {
        ImGui::TextDisabled("Runtime found at $%06X. Checking this installs the Triple pickup hook.", routineAddress);
    } else {
        ImGui::TextDisabled("Checking this hooks thrown axes and Triple mode for breakable block events.");
    }
}

static void DrawSelectedEventProperties(EditorState& state)
{
    SC4Core& core = state.session.Core();
    EventInfo* event = SelectedEvent(state);
    if (ImGui::CollapsingHeader("Selected Event", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!event) {
            ImGui::TextDisabled("Click an event marker in the level view.");
            return;
        }

        ImGui::Text("%s", EventDisplayName(core, *event));
        ImGui::TextDisabled("Event %d of %d", state.selectedEventIndex + 1, static_cast<int>(core.eventTable.size()));
        if (ImGui::Button("Sort Events")) {
            PushUndo(state);
            const EventInfo selectedCopy = *event;
            state.session.SortEvents();
            state.selectedEventIndex = FindMatchingEventIndex(core.eventTable, selectedCopy);
            state.levelRenderer.Invalidate();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Slot Events")) {
            PushUndo(state);
            const EventInfo selectedCopy = *event;
            state.session.SlotEvents();
            state.selectedEventIndex = FindMatchingEventIndex(core.eventTable, selectedCopy);
            state.levelRenderer.Invalidate();
            return;
        }
        ImGui::Separator();

        bool changed = false;
        const EventInfo beforeEdit = *event;
        int type = static_cast<int>(event->type);
        const std::vector<std::string> eventTypes = { "Enemy", "Candle", "Object", "Special" };
        changed |= ComboRow("Type", type, eventTypes);
        if (type < 0) {
            type = 0;
        } else if (type > 3) {
            type = 3;
        }
        event->type = static_cast<BYTE>(type);

        unsigned value = event->xpos;
        if (DrawEventNumberField("X", value, 2)) {
            event->xpos = static_cast<WORD>(value);
            changed = true;
        }
        value = event->ypos;
        if (DrawEventNumberField("Y", value, 2)) {
            event->ypos = static_cast<WORD>(value);
            changed = true;
        }
        value = event->eventId;
        if (DrawEventNumberField("ID", value, 2)) {
            event->eventId = static_cast<WORD>(value);
            changed = true;
        }
        value = event->eventSubId;
        if (DrawEventNumberField("Sub ID", value, 2)) {
            event->eventSubId = static_cast<WORD>(value);
            changed = true;
        }
        value = event->spawnIndex;
        if (DrawEventNumberField("Spawn index", value, 2)) {
            event->spawnIndex = static_cast<WORD>(value);
            changed = true;
        }
        value = event->match;
        if (DrawEventNumberField("Match", value, 2)) {
            event->match = static_cast<WORD>(value);
            changed = true;
        }
        value = event->unknown;
        if (DrawEventNumberField("Unknown", value, 2)) {
            event->unknown = static_cast<WORD>(value);
            changed = true;
        }

        if (changed) {
            const EventInfo afterEdit = *event;
            *event = beforeEdit;
            PushUndo(state);
            *event = afterEdit;
            state.session.SaveEvents();
            state.levelRenderer.Invalidate();
        }
    }
}

static void DrawPlayerProperties(EditorState& state)
{
    if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen)) {
        ComboRow("Whip", g_propertyState.whip, { "Leather", "Chain0", "Chain1" });
        const unsigned whip = static_cast<unsigned>(g_propertyState.whip);
        DrawNumberProperty(state, "Whip length", 2, { 0x819261 + 2 * whip });
        DrawNumberProperty(state, "Whip full damage", 2, { 0x81A6EC + 4 * whip });
        DrawNumberProperty(state, "Whip partial damage", 2, { 0x81A6EC + 4 * whip + 2 });

        ComboRow("Subweapon", g_propertyState.subweapon, { "Knife", "Axe", "Holy Water", "Cross" });
        DrawNumberProperty(state, "Subweapon damage", 2, { SUBWEAPON_DAMAGE_BASE + 2 * (static_cast<unsigned>(g_propertyState.subweapon) + 1) });
        DrawKnifePlatformPickupProperty(state);
        DrawAxeBlockBreakerProperty(state);

        static const std::vector<MovementProperty> movements = {
            { "Walking Right", { 0x80A665 }, 0x80A65F, false, true },
            { "Walking Left", { 0x80A67E }, 0x80A678, true, true },
            { "Jumping Right", { 0x80A90B, 0x80A910 }, 0x80A916, false, true },
            { "Jumping Left", { 0x80A93C, 0x80A941 }, 0x80A947, true, true },
            { "Crouching Right", { 0x80A705 }, 0x80A6FF, false, true },
            { "Crouching Left", { 0x80A716 }, 0x80A710, true, true },
            { "Climbing Right", { 0x80A8C8 }, 0x80A8C2, false, true },
            { "Climbing Left", { 0x80A8A6 }, 0x80A8A0, true, true },
            { "Gravity", {}, 0x80A73E, false, false },
        };
        std::vector<std::string> movementNames;
        movementNames.reserve(movements.size());
        for (const MovementProperty& movement : movements) {
            movementNames.emplace_back(movement.name);
        }
        ComboRow("Movement", g_propertyState.movement, movementNames);
        if (g_propertyState.movement < 0 || g_propertyState.movement >= static_cast<int>(movements.size())) {
            g_propertyState.movement = 0;
        }
        const MovementProperty& movement = movements[static_cast<size_t>(g_propertyState.movement)];
        if (movement.inverted) {
            DrawInvertedNumberProperty(state, "Move pixels", movement.pixelAddresses, movement.hasPixels);
            DrawInvertedNumberProperty(state, "Move subpixels", { movement.subpixelAddress });
        } else {
            DrawNumberProperty(state, "Move pixels", 2, movement.pixelAddresses, movement.hasPixels);
            DrawNumberProperty(state, "Move subpixels", 2, { movement.subpixelAddress });
        }
    }
}

static void DrawLevelProperties(EditorState& state)
{
    if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen)) {
        const unsigned deathBase = state.session.Region() == 0 ? 0x81B395 : 0x81B369;
        DrawNumberProperty(state, "Type", 2, { LevelAddress(0x868296, state, 2) });
        DrawNumberProperty(state, "Death level", 1, { LevelAddress(deathBase, state) });
        DrawNumberProperty(state, "Continue level", 1, { LevelAddress(0x81FBAC, state) });
        DrawNumberProperty(state, "Music", 1, { LevelAddress(0x8097C3, state) });
        DrawNumberProperty(state, "Layer mask", 2, { LevelAddress(0x85C7BE, state, 2) });
        DrawFlaggedWordProperty(state, "Layer behavior", "Layer behavior flag", { LevelAddress(0x85C846, state, 2) }, 0x8000);
        DrawNumberProperty(state, "Event direction", 1, { LevelAddress(0x80D8A3, state) });
        DrawNumberProperty(state, "BG animation 0", 2, { LevelAddress(0x85CA82, state, 2) });
        DrawNumberProperty(state, "BG animation 1", 2, { LevelAddress(0x85CB0A, state, 2) });
        DrawNumberProperty(state, "Palette animation", 2, { LevelAddress(0x86946F, state, 2) });
        DrawNumberProperty(state, "Enemy set ID", 2, { LevelAddress(0x868BCD, state, 2) });
        DrawNumberProperty(state, "Enemy set", 2, { LevelAddress(0x868B45, state, 2) });
        DrawCurrentLevelEnemies(state);
    }
}

static void DrawExpandedProperties(EditorState& state)
{
    const bool expanded = state.session.IsExpandedRom();
    if (ImGui::CollapsingHeader("Expanded ROM", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!expanded) {
            ImGui::TextDisabled("These settings are available for expanded ROMs.");
        }

        static const std::vector<std::string> entrances = { "0", "1", "2", "3", "4", "5", "6", "7" };
        static const std::vector<std::string> exitTypes = { "Init (DONT USE)", "Stairs Up", "Stairs Down", "Left", "Right" };
        static const std::vector<std::string> cameraLocks = NumberItems(0x20);
        static const std::vector<std::string> exitChecks = NumberItems(0x40);

        ImGui::BeginDisabled(!expanded);
        ComboRow("Checkpoint", g_propertyState.checkpoint, entrances);
        const unsigned entranceBase = 0xA78000 + 0x100 * static_cast<unsigned>(state.level) + 0x20 * static_cast<unsigned>(g_propertyState.checkpoint);
        DrawNumberProperty(state, "State0", 1, { entranceBase + 0x0 }, expanded);
        DrawNumberProperty(state, "State1", 2, { entranceBase + 0xE }, expanded);
        DrawNumberProperty(state, "X pos", 2, { entranceBase + 0x2 }, expanded);
        DrawNumberProperty(state, "Y pos", 2, { entranceBase + 0x4 }, expanded);
        DrawNumberProperty(state, "Cam0 X", 2, { entranceBase + 0x6 }, expanded);
        DrawNumberProperty(state, "Cam0 Y", 2, { entranceBase + 0x8 }, expanded);
        DrawNumberProperty(state, "Cam1 X", 2, { entranceBase + 0xA }, expanded);
        DrawNumberProperty(state, "Cam1 Y", 2, { entranceBase + 0xC }, expanded);
        DrawNumberProperty(state, "Camera left", 2, { entranceBase + 0x12 }, expanded);
        DrawNumberProperty(state, "Camera right", 2, { entranceBase + 0x14 }, expanded);
        DrawNumberProperty(state, "Camera top", 2, { entranceBase + 0x16 }, expanded);
        DrawNumberProperty(state, "Camera bottom", 2, { entranceBase + 0x18 }, expanded);
        DrawNumberProperty(state, "Camera speed X", 2, { entranceBase + 0x1A }, expanded);
        DrawNumberProperty(state, "Camera speed Y", 2, { entranceBase + 0x1C }, expanded);
        DrawNumberProperty(state, "Camera pointer", 2, { entranceBase + 0x1E }, expanded);

        ImGui::Separator();
        ComboRow("Camera lock", g_propertyState.cameraLock, cameraLocks);
        const unsigned lockBase = 0xA58000 + 0xC * 0x20 * static_cast<unsigned>(state.level) + 0xC * static_cast<unsigned>(g_propertyState.cameraLock);
        DrawNumberProperty(state, "Lock direction", 2, { lockBase + 0x0 }, expanded);
        DrawNumberProperty(state, "Lock dir addr", 2, { lockBase + 0x2 }, expanded);
        DrawNumberProperty(state, "Lock cmp addr", 2, { lockBase + 0x4 }, expanded);
        DrawNumberProperty(state, "Lock cmp value", 2, { lockBase + 0x6 }, expanded);
        DrawNumberProperty(state, "Lock store value", 2, { lockBase + 0x8 }, expanded);
        DrawNumberProperty(state, "Lock store addr", 2, { lockBase + 0xA }, expanded);

        ImGui::Separator();
        ComboRow("Next level direction", g_propertyState.nextLevelDirection, entrances);
        const unsigned transitionBase = 0xA0C000 + 0x10 * static_cast<unsigned>(state.level) + 0x2 * static_cast<unsigned>(g_propertyState.nextLevelDirection);
        DrawNumberProperty(state, "Next level", 1, { transitionBase + 0x0 }, expanded);
        DrawNumberProperty(state, "Entrance num", 1, { transitionBase + 0x1 }, expanded);
        DrawNumberProperty(state, "Death num", 1, { entranceBase + 0x1 }, expanded);

        ImGui::Separator();
        ComboRow("Exit check", g_propertyState.exitCheck, exitChecks);
        const unsigned exitBase = 0xA68000 + 0x40 * 0x4 * static_cast<unsigned>(state.level) + 0x4 * static_cast<unsigned>(g_propertyState.exitCheck);
        DrawComboProperty(state, "Exit type", g_propertyState.exitType, exitTypes, 1, { exitBase + 0x0 }, expanded);
        DrawNumberProperty(state, "Exit type value", 1, { exitBase + 0x0 }, expanded);
        DrawNumberProperty(state, "Exit num", 1, { exitBase + 0x1 }, expanded);
        DrawNumberProperty(state, "Exit cmp value", 2, { exitBase + 0x2 }, expanded);
        ImGui::EndDisabled();
    }
}

}

void DrawGlobalPropertiesTab(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Open a ROM to edit global properties.");
        return;
    }

    ImGui::BeginChild("global-properties-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    DrawGeneralProperties(state);
    DrawPlayerProperties(state);
    ImGui::EndChild();
}

void DrawLevelPropertiesTab(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Open a ROM to edit level properties.");
        return;
    }

    ImGui::BeginChild("level-properties-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    DrawLevelProperties(state);
    DrawExpandedProperties(state);
    ImGui::EndChild();
}

void DrawSelectionTab(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Open a ROM to edit selected items.");
        return;
    }

    ImGui::BeginChild("selection-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    DrawSelectedEventProperties(state);
    ImGui::EndChild();
}
