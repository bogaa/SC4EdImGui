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
    
    // data tables level
    static constexpr unsigned LEVEL_BG_PROPERTY_MASK_BASE = 0x85C7BE; // transparency and such
    static constexpr unsigned LEVEL_BG_SCROLL_BASE = 0x85C846;  // done different..
    static constexpr unsigned LEVEL_TILE1_ANIMATION_POINTER_BASE = 0x85C846;
    static constexpr unsigned LEVEL_TILE2_ANIMATION_POINTER_BASE = 0x85cb0a;
    static constexpr unsigned LEVEL_PALETTE_ANIMATION_POINTER_BASE = 0x86946f;
    
    // data tables event
    static constexpr unsigned SUBWEAPON_DAMAGE_BASE = 0x81A6F8; 
    static constexpr unsigned EVENT_BREAKABLE_WALL_ITEM_BASE = 0x81A81A;
    static constexpr unsigned EVENT_HITBOX_BASE = 0x81ab00;
    static constexpr unsigned EVENT_HEALTH_BASE = 0x81ac00;
    static constexpr unsigned EVENT_HIT_ATTRIBUTE_BASE = 0x81ad00; // 01 hurt, 04 whip hitable, 08 collect able also needs bit 01 set, 10 ??, 20 ??, 40 rossery, 80 noDespawn 
    static constexpr unsigned EVENT_DEATH_ANIMATION_BASE = 0x81ae00;
    static constexpr unsigned EVENT_DEATH_MOVBITS_BASE = 0x81ae80;
    static constexpr unsigned EVENT_DAMAGE_BASE = 0x81af00;
    
    // routines
    // static constexpr unsigned TRIPLE_SHOT_PICKUP_JML = 0x80DFA3;
    // static constexpr unsigned AXE_STATE01_HOOK = 0x80BB05;
    // static constexpr unsigned KNIFE_STATE_POINTER = 0x80BA50;
    // static constexpr unsigned KNIFE_STATE_JML_STUB = 0x80FEDB;
    // static constexpr unsigned CLEAR_SELECTED_EVENT_SLOT_ALL = 0x808C59;
    // static constexpr unsigned LUNCH_SFX_FROM_ACCUM = 0x8085E3;
    // static constexpr unsigned READ_COLLISION_TABLE_7E4000 = 0x80CF86;
    // static constexpr unsigned MAKE_THIS_ENTITY_PLATFORM = 0x82C312;
    // static constexpr unsigned AXE_COUNTER = 0x80BB3A;
    // static constexpr unsigned AXE_ANIMATION = 0x80BB44;
    // static constexpr unsigned AXE_SPEED_MOVEMENT = 0x80BB11;
    // static constexpr unsigned CRUMBLE_BLOCK_BURN = 0x8290D1;

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

        if (ImGui::InputInt("##value", &value, 1, 8, ImGuiInputTextFlags_AutoSelectAll)) {
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

    static void DrawBitfieldWordProperty(EditorState& state, const char* label, const std::vector<unsigned>& addresses, bool enabled = true)
    {
        RomSession& session = state.session;
        const bool canEdit = session.IsLoaded() && enabled && !addresses.empty() && addresses.front() != 0;
        const unsigned raw = canEdit ? session.ReadRom(addresses.front(), 2) & 0xFFFFu : 0;
        unsigned newValue = raw;

        ImGui::BeginDisabled(!canEdit);
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::TextDisabled("%u (0x%04X)", raw, raw);

        for (unsigned bit = 0; bit < 16; ++bit) {
            if (bit != 0) {
                ImGui::SameLine();
            }

            bool enabledBit = (raw & (1u << bit)) != 0;
            ImGui::PushID(static_cast<int>(bit));
            if (ImGui::Checkbox("##bit", &enabledBit)) {
                if (enabledBit) {
                    newValue |= 1u << bit;
                } else {
                    newValue &= ~(1u << bit);
                }
            }
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::Text("%u", bit);
            ImGui::PopID();
        }

        if (ImGui::IsItemHovered() && !addresses.empty()) {
            ImGui::SetTooltip("ROM address: %06X", addresses.front());
        }

        if (newValue != raw) {
            session.WriteRomAll(addresses, 2, newValue);
            state.levelRenderer.Invalidate();
        }

        ImGui::PopID();
        ImGui::EndDisabled();
    }

    static void DrawBitfieldByteProperty(EditorState& state, const char* label, const std::vector<unsigned>& addresses, bool enabled = true)
    {
        RomSession& session = state.session;
        const bool canEdit = session.IsLoaded() && enabled && !addresses.empty() && addresses.front() != 0;
        const unsigned raw = canEdit ? session.ReadRom(addresses.front(), 1) & 0xFFu : 0;
        unsigned newValue = raw;

        ImGui::BeginDisabled(!canEdit);
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::TextDisabled("%u (0x%02X)", raw, raw);

        for (unsigned bit = 0; bit < 8; ++bit) {
            if (bit != 0) {
                ImGui::SameLine();
            }

            bool enabledBit = (raw & (1u << bit)) != 0;
            ImGui::PushID(static_cast<int>(bit));
            if (ImGui::Checkbox("##bit", &enabledBit)) {
                if (enabledBit) {
                    newValue |= 1u << bit;
                } else {
                    newValue &= ~(1u << bit);
                }
            }
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::Text("%u", bit);
            ImGui::PopID();
        }

        if (ImGui::IsItemHovered() && !addresses.empty()) {
            if (addresses.size() == 1) {
                ImGui::SetTooltip("ROM address: %06X", addresses.front());
            } else {
                ImGui::SetTooltip("Writes %zu mirrored ROM addresses", addresses.size());
            }
        }

        if (newValue != raw) {
            session.WriteRomAll(addresses, 1, newValue);
            state.levelRenderer.Invalidate();
        }

        ImGui::PopID();
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
        if (ImGui::InputInt("##value", &value, 1, 4, ImGuiInputTextFlags_AutoSelectAll)) {
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
       
        if (ImGui::InputInt("##value", &value, 1, 4, ImGuiInputTextFlags_AutoSelectAll)) {         
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
        ImGui::TextUnformatted("Enemie set for current level");
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
    
        
        }
    }

//struct KnifePickupModeLocation {


    static void DrawSelectedEventProperties(EditorState& state)
    {
        SC4Core& core = state.session.Core();
        EventInfo* event = SelectedEvent(state);   
        
        if (event != nullptr && event->eventId != 0) {   // Can we read from event pointer. Else it could crash the editor.
        
            if (ImGui::CollapsingHeader("Selected Event", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!event) {
                    ImGui::TextDisabled("Click an event marker in the level view.");
                    return;
                }
    
                ImGui::Text("%s", EventDisplayName(core, *event));
    
                if (ImGui::Button("Sort Events")) {
                    PushUndo(state);
                    const EventInfo selectedCopy = *event;
                    state.session.SortEvents();
                    state.selectedEventIndex = FindMatchingEventIndex(core.eventTable, selectedCopy);
                    state.levelRenderer.Invalidate();
                    return;
                }
               ImGui::SameLine();
               
               bool disabled = true;  // This probably just confuses and I never used it. 
               ImGui::BeginDisabled(disabled);
               if (ImGui::Button("Slot Events")) {
                   PushUndo(state);
                   const EventInfo selectedCopy = *event;
                   state.session.SlotEvents();
                   state.selectedEventIndex = FindMatchingEventIndex(core.eventTable, selectedCopy);
                   state.levelRenderer.Invalidate();
                   return;
                }
               ImGui::EndDisabled();
               
                ImGui::Separator();
    
                bool changed = false;
                const EventInfo beforeEdit = *event;
                int type = static_cast<int>(event->type);
                const std::vector<std::string> eventTypes = { "entity_respawn", "Candle", "entity_presist", "Unused" };
                changed |= ComboRow("Type", type, eventTypes);
                if (type < 0) {
                    type = 0;
                } else if (type > 3) {
                    type = 3;
                }
                
                event->type = (static_cast<BYTE>(type) & 0x03u);
                unsigned value = (event->xpos) & 0x3FFCu;                     // FIXME shows xpos properly.. we need to fix where it renders! May be it fixes it here too
                if (DrawEventNumberField("X", value, 2)) {
                    event->xpos = static_cast<WORD>(value);
                    if (type == 1) {
                        event->eventSubId = static_cast<WORD>((event->eventSubId & 0xF0u) | ((event->xpos >> 12) & 0x0Fu));
                    }
                    changed = true;
                }
                value = (event->ypos) & 0x3FFCu;
                if (DrawEventNumberField("Y", value, 2)) {
                    event->ypos = static_cast<WORD>(value);
                    if (type == 1) {
                        event->eventSubId = static_cast<WORD>((event->eventSubId & 0x0Fu) | ((event->ypos >> 8) & 0xF0u));
                    }
                    changed = true;
                }
                value = (event->eventId) & 0x00FFu;
                if (DrawEventNumberField("ID", value, 2)) {
                    event->eventId = static_cast<WORD>(value);
                    changed = true;
                }
                value = (event->eventSubId) & 0x00FFu;  
                    if (type == 1) {
                        if (DrawEventNumberField("Mask", value, 2)) {
                        event->eventSubId = static_cast<WORD>(value);
                        //    event->xpos = static_cast<WORD>((event->xpos & 0xC003u) | ((event->eventSubId & 0x0Fu) << 12)); FIXME I may have fixed syncing evnt bits encoding in a better way..
                        //    event->ypos = static_cast<WORD>((event->ypos & 0xC003u) | ((event->eventSubId & 0xF0u) << 8));
                        changed = true;
                        }
                        //      value = (event->eventSubId) & 0x00FFu;       //
                        //      ImGui::TextDisabled("HEX %X (0x4 = Quest, 0x8 = Background, 0xC both)", value);
                    }
                    else if (type != 1) {
                        if (DrawEventNumberField("SubID", value, 2)) {       
                        event->eventSubId = static_cast<WORD>(value);
                        changed = true;
                        }
                        ImGui::SetCursorPosX( + 20);
                        ImGui::TextDisabled("24 to 36 Drops ItemID");
                    }
                
                value = (event->unknown) & 0x0003u;                          // will always be 3 never changes.. probably breaks things. 
                if (DrawEventNumberField("Unknown", value, 2)) {
                    event->unknown = static_cast<WORD>(value);
                    changed = true;
                }
                
                value = (event->match) & 0x00FFu;        
                if (type == 0) {        
                    if (DrawEventNumberField("Mask", value, 2)) {
                    event->match = static_cast<WORD>(value);
                    changed = true;                  
                    }
                    // value = (event->match) & 0x00FFu;                    FIXME make a working bit field. 
                    // DrawBitfieldByteProperty(state, "donno, donno, Quest, Background", {value & 0x00FF});
                    value = (event->match) & 0x00FFu;
                    ImGui::SetCursorPosX( + 20);
                    ImGui::TextDisabled("0x%X (0x4 = Quest, 0x8 = Background, 0xC both)", value);
                }
                else if (type >= 1) {                                       // candles and respawning events use index table at WRAM 0x1500
                    if (DrawEventNumberField("Spawn mask", value, 2)) {
                    event->match = static_cast<WORD>(value);
                    changed = true;
                    }
                }
                
                value = (event->eventId) & 0x00FFu;                         // field with hex number as info 
                ImGui::SetCursorPosX(+20);
                ImGui::TextDisabled("Event ID $%X", value);
                value = (event->eventSubId) & 0x00FFu;
				ImGui::SameLine();
                ImGui::TextDisabled("sub $%X", value);
                value = (event->xpos) & 0x3FFCu;
                ImGui::SameLine();
                ImGui::TextDisabled("    xpos $%X", value);
                value = (event->ypos) & 0x3FFCu;
                ImGui::SameLine();
                ImGui::TextDisabled("ypos $%X", value);
                ImGui::Separator();

                ImGui::TextDisabled("Event %d of %d", state.selectedEventIndex + 1, static_cast<int>(core.eventTable.size()));
                if (ImGui::Button("Prev Event")) {
                    const int total = static_cast<int>(core.eventTable.size());
                    if (total > 0) {
                        if (state.selectedEventIndex < 0) {
                            state.selectedEventIndex = 0;                
                        }
                        else {
                            state.selectedEventIndex = (state.selectedEventIndex - 1) % total;
                        }
                        state.levelRenderer.Invalidate();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Next Event")) {
                    const int total = static_cast<int>(core.eventTable.size());
                    if (total > 0) {
                        if (state.selectedEventIndex < 0) {
                            state.selectedEventIndex = 0;
                            state.selectedEventIndex >= int(core.eventTable.size());
                        }
                        else {
                            state.selectedEventIndex = (state.selectedEventIndex + 1) % total;
                        }
                        state.levelRenderer.Invalidate();
                    }
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
        
            if (ImGui::CollapsingHeader("Event Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
    
                DrawNumberProperty(state, "Hitbox X", 1, { EVENT_HITBOX_BASE + (event->eventId) * 2 });
                DrawNumberProperty(state, "Hitbox Y", 1, { EVENT_HITBOX_BASE + 1 + (event->eventId) * 2 });
                DrawNumberProperty(state, "Health", 2, { EVENT_HEALTH_BASE + (event->eventId) * 2 });
                DrawNumberProperty(state, "Damage", 1, { EVENT_DAMAGE_BASE + (event->eventId) });
               
                ImGui::TextDisabled("Some events overwrite there attributes in ther code.");
                DrawBitfieldByteProperty(state, "hurt subW whip col ?? ?? msk rosry noDesp", { EVENT_HIT_ATTRIBUTE_BASE + (event->eventId) * 2 });
             

                ImGui::Separator();
			//	ImGui::TextDisabled("Edit with cosion game might crash!");
                DrawNumberProperty(state, "!Death spawnID (Flame)", 1, { EVENT_DEATH_ANIMATION_BASE + (event->eventId) }); 
                DrawNumberProperty(state, "!Death Movement Bits", 1, { EVENT_DEATH_MOVBITS_BASE + (event->eventId) });
                
                
                
                ImGui::Separator();

                if (event->eventId == 0x2F) {
                    DrawNumberProperty(state, "Breakable Wall Item", 1, { EVENT_BREAKABLE_WALL_ITEM_BASE + ((event->eventSubId) & 0x0F) });
                }
                
                const bool expanded = state.session.IsExpandedRom();
                if (!expanded) {
                    ImGui::TextDisabled("These settings are available for expanded ROMs.");
                }               
                ImGui::BeginDisabled(!expanded);
                if (event->eventId == 0x15) {      
                    static const std::vector<std::string> entrances = { "0", "1", "2", "3", "4", "5", "6", "7" };
                    static const std::vector<std::string> exitTypes = { "Init (DONT USE)", "Stairs Up", "Stairs Down", "Left", "Right" };
                    static const std::vector<std::string> exitChecks = NumberItems(0x40);
                                   
                    ImGui::TextDisabled("Exit Event Editor");
                    ImGui::Separator();

                    ComboRow("Exit SubID", g_propertyState.exitCheck, exitChecks);
                    const unsigned exitBase = 0xA68000 + 0x40 * 0x4 * static_cast<unsigned>(state.level) + 0x4 * static_cast<unsigned>(g_propertyState.exitCheck);
                    DrawComboProperty(state, "Exit type", g_propertyState.exitType, exitTypes, 1, { exitBase + 0x0 }, expanded);
                    //DrawNumberProperty(state, "Exit type value", 1, { exitBase + 0x0 }, expanded);
                    DrawNumberProperty(state, "Exit cmp value X or Y", 2, { exitBase + 0x2 }, expanded);
                    DrawNumberProperty(state, "Transit num for transit", 1, { exitBase + 0x1 }, expanded);

                    ImGui::TextDisabled("Level Transit Editor");
                    ImGui::Separator();

                    ComboRow("Next level checkpoint", g_propertyState.nextLevelDirection, entrances);
                    const unsigned transitionBase = 0xA0C000 + 0x10 * static_cast<unsigned>(state.level) + 0x2 * static_cast<unsigned>(g_propertyState.nextLevelDirection);
                    DrawNumberProperty(state, "Transit num of event", 1, { transitionBase + 0x1 }, expanded);
                    DrawNumberProperty(state, "Next level", 1, { transitionBase + 0x0 }, expanded);

                    ImGui::Spacing();
                    ImGui::Spacing();
                
                }
                
                if (event->eventId == 0x41) {
                    
                    static const std::vector<std::string> cameraLocks = NumberItems(0x20);
                    
                    ImGui::TextDisabled("Camlock");
                    ImGui::Separator();

                    ComboRow("Camera lock", g_propertyState.cameraLock, cameraLocks);
                    const unsigned lockBase = 0xA58000 + 0xC * 0x20 * static_cast<unsigned>(state.level) + 0xC * static_cast<unsigned>(g_propertyState.cameraLock);
                    DrawNumberProperty(state, "Lock direction", 2, { lockBase + 0x0 }, expanded);
                    DrawNumberProperty(state, "Lock dir addr", 2, { lockBase + 0x2 }, expanded);
                    DrawNumberProperty(state, "Lock cmp addr", 2, { lockBase + 0x4 }, expanded);
                    DrawNumberProperty(state, "Lock cmp value", 2, { lockBase + 0x6 }, expanded);
                    DrawNumberProperty(state, "Lock store value", 2, { lockBase + 0x8 }, expanded);
                    DrawNumberProperty(state, "Lock store addr", 2, { lockBase + 0xA }, expanded);
                }
                ImGui::EndDisabled();
            
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
    //        DrawKnifePlatformPickupProperty(state);   // function is moved to ..bkp/trash.txt and the top line is dublicated and documented out here
    //        DrawAxeBlockBreakerProperty(state);
    
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
    
            DrawNumberProperty(state, "Death level", 1, { LevelAddress(deathBase, state) });
            DrawNumberProperty(state, "Continue level", 1, { LevelAddress(0x81FBAC, state) });
            DrawNumberProperty(state, "Music", 1, { LevelAddress(0x8097C3, state) });
            DrawNumberProperty(state, "Timer", 2, { LevelAddress(0x85BCF8, state, 2) });    // FIXME This is already decimal in the rom 
            DrawNumberProperty(state, "Enemy Damage Buff", 1, { LevelAddress(0x81A88F, state, 1) });
            
            DrawNumberProperty(state, "Level type, layout, mode and other properties", 2, { LevelAddress(0x868296, state, 2) });
            DrawNumberProperty(state, "Layer mask", 2, { LevelAddress(0x85C7BE, state, 2) });
            DrawFlaggedWordProperty(state, "Layer behavior", "Layer behavior flag", { LevelAddress(0x85C846, state, 2) }, 0x8000);
            DrawNumberProperty(state, "Event direction", 1, { LevelAddress(0x80D8A3, state) });
           //DrawNumberProperty(state, "BG animation 0", 2, { LevelAddress(0x85CA82, state, 2) });
           //DrawNumberProperty(state, "BG animation 1", 2, { LevelAddress(0x85CB0A, state, 2) });
           //DrawNumberProperty(state, "Palette animation", 2, { LevelAddress(0x86946F, state, 2) });
           //DrawNumberProperty(state, "Enemy set ID", 2, { LevelAddress(0x868BCD, state, 2) });
           //DrawNumberProperty(state, "Enemy set", 2, { LevelAddress(0x868B45, state, 2) });
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
    //        static const std::vector<std::string> exitTypes = { "Init (DONT USE)", "Stairs Up", "Stairs Down", "Left", "Right" };
    //        static const std::vector<std::string> cameraLocks = NumberItems(0x20);
    //        static const std::vector<std::string> exitChecks = NumberItems(0x40);
    
            ImGui::BeginDisabled(!expanded);
            ImGui::Separator();
            ImGui::TextDisabled("Entrance property for each checkpoint");
            ImGui::Separator();
    
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
    
            DrawNumberProperty(state, "Death level", 1, { entranceBase + 0x1 }, expanded);
    
    
            ImGui::Spacing();
            ImGui::Spacing();
 
             //  ImGui::Separator();       // This is moved to the event dialog 
             //  ImGui::TextDisabled("Exit event ID 21");     
             //  ImGui::Separator();
             //
             //  ComboRow("Exit SubID", g_propertyState.exitCheck, exitChecks);
             //  const unsigned exitBase = 0xA68000 + 0x40 * 0x4 * static_cast<unsigned>(state.level) + 0x4 * static_cast<unsigned>(g_propertyState.exitCheck);
             //  DrawComboProperty(state, "Exit type", g_propertyState.exitType, exitTypes, 1, { exitBase + 0x0 }, expanded);
             //  //DrawNumberProperty(state, "Exit type value", 1, { exitBase + 0x0 }, expanded);
             //  DrawNumberProperty(state, "Exit cmp value Y", 2, { exitBase + 0x2 }, expanded); 
             //  DrawNumberProperty(state, "Transit num for transit", 1, { exitBase + 0x1 }, expanded);
             //  
             //  ImGui::TextDisabled("Exit level transit");
             //  ImGui::Separator();
             //
             //  ComboRow("Next level checkpoint", g_propertyState.nextLevelDirection, entrances);
             //  const unsigned transitionBase = 0xA0C000 + 0x10 * static_cast<unsigned>(state.level) + 0x2 * static_cast<unsigned>(g_propertyState.nextLevelDirection);
             //  DrawNumberProperty(state, "Transit num of event", 1, { transitionBase + 0x1 }, expanded);
             //  DrawNumberProperty(state, "Next level", 1, { transitionBase + 0x0 }, expanded);
             //
             //  
             //  ImGui::Spacing();
             //  ImGui::Spacing();
             //  ImGui::Separator();
             //  ImGui::TextDisabled("Camlock event ID 65");
             //  ImGui::Separator();
             //
             //  ComboRow("Camera lock", g_propertyState.cameraLock, cameraLocks);
             //  const unsigned lockBase = 0xA58000 + 0xC * 0x20 * static_cast<unsigned>(state.level) + 0xC * static_cast<unsigned>(g_propertyState.cameraLock);
             //  DrawNumberProperty(state, "Lock direction", 2, { lockBase + 0x0 }, expanded);
             //  DrawNumberProperty(state, "Lock dir addr", 2, { lockBase + 0x2 }, expanded);
             //  DrawNumberProperty(state, "Lock cmp addr", 2, { lockBase + 0x4 }, expanded);
             //  DrawNumberProperty(state, "Lock cmp value", 2, { lockBase + 0x6 }, expanded);
             //  DrawNumberProperty(state, "Lock store value", 2, { lockBase + 0x8 }, expanded);
             //  DrawNumberProperty(state, "Lock store addr", 2, { lockBase + 0xA }, expanded);
            
            ImGui::EndDisabled();
        
        }
    }

} // namespace



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
