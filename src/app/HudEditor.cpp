#include "HudEditor.h"

#include "EditorState.h"
#include "EditorUndo.h"

#include "SC4Core.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

struct HudWordField {
    const char* label;
    unsigned address;
    const char* note;
};

struct HudPositionField {
    const char* label;
    unsigned address;
    const char* note;
    bool tileOffset;
};

struct HudScannedPosition {
    const char* label;
    unsigned valueAddress;
    unsigned routineAddress;
    uint16_t value;
    int x;
    int y;
    bool tileOffset;
    char reg;
};

constexpr std::array<HudPositionField, 3> kHudPositionFields = {{
    { "Subweapon Y", 0x80C6D8, "LDY #$081C from $80C6D7", false },
    { "Heart position", 0x80C760, "LDY #$5859 from $80C75F", true },
    { "Multishot X/Y", 0x80C6F8, "LDY #$0040 from $80C6F7", false },
}};

constexpr std::array<HudPositionField, 6> kHudStaticTextFields = {{
    { "HUD construct 00", 0x81A259, "HUD_Construct00 dw $583B", true },
    { "HUD construct 01", 0x81A3B3, "HUD_Construct01 dw $5802", true },
    { "Score border/value", 0x81A3C3, "pointerPPU_score_00_border00 dw $5820", true },
    { "Top-right value", 0x81A3D1, "pointerPPU_XX02 dw $5842", true },
    { "Enemy/AMMO text", 0x81A3DD, "pointerPPU_enemy dw $5852", true },
    { "Bottom-right border", 0x81A3E8, "pointerPPU_XX03 dw $5862", true },
}};

constexpr std::array<HudWordField, 1> kHudCounterFields = {{
    { "HUD table count", 0x80C707, "CMP #$0003 from $80C706" },
}};

constexpr std::array<HudWordField, 4> kHudPointerFields = {{
    { "HUD GFX src/des pointer", 0x80C64F, "LDX #$B4A3 from $80C64E" },
    { "HUD construct 01", 0x80C65A, "LDX #HUD_Construct01 from $80C659" },
    { "HUD construct 00", 0x80C661, "LDX #HUD_Construct00 from $80C660" },
    { "Level number table", 0x80C670, "ADC #levelNumberTableHUD from $80C66F" },
}};

constexpr std::array<HudWordField, 6> kHudUpdateTableFields = {{
    { "Score update", 0x80C71D, "dw $C729" },
    { "Simon health update", 0x80C71F, "dw $C73E" },
    { "Boss health update", 0x80C721, "dw $C747" },
    { "Timer update", 0x80C723, "dw $C74F" },
    { "Hearts update", 0x80C725, "dw $C75F" },
    { "Life update", 0x80C727, "dw $C76F" },
}};

constexpr std::array<HudWordField, 6> kSubweaponUpgradeFields = {{
    { "Upgrade word 0", 0x81A249, "dw $8190" },
    { "Upgrade word 1", 0x81A24B, "dw $3226" },
    { "Upgrade word 2", 0x81A24D, "dw $3228" },
    { "Upgrade word 3", 0x81A24F, "dw $322A" },
    { "Upgrade word 4", 0x81A251, "dw $3244" },
    { "Upgrade word 5", 0x81A253, "dw $3248" },
}};

bool IsValidPcOffset(const SC4Core& core, unsigned pc, unsigned bytes)
{
    return core.rom && pc < core.romSize && bytes <= core.romSize - pc;
}

const char* HudPositionLabel(unsigned instructionAddress)
{
    switch (instructionAddress) {
    case 0x80C6D7: return "Subweapon Y";
    case 0x80C6E4: return "HUD setup position";
    case 0x80C6F7: return "Multishot X/Y";
    case 0x80C729: return "Score position";
    case 0x80C73E: return "Simon health position";
    case 0x80C747: return "Boss health position";
    case 0x80C74F: return "Timer position";
    case 0x80C75F: return "Hearts position";
    case 0x80C76F: return "Life position";
    default: break;
    }

    if (instructionAddress < 0x80C71D) {
        return "HUD setup position";
    }
    if (instructionAddress < 0x80C73E) {
        return "Score position";
    }
    if (instructionAddress < 0x80C747) {
        return "Simon health position";
    }
    if (instructionAddress < 0x80C74F) {
        return "Boss health position";
    }
    if (instructionAddress < 0x80C75F) {
        return "Timer position";
    }
    if (instructionAddress < 0x80C76F) {
        return "Hearts position";
    }
    return "Life position";
}

bool IsLikelyHudPosition(uint8_t op, uint16_t value)
{
    if (op == 0xA0 && (value == 0x0080 || value == 0x008C || value == 0x14F1)) {
        return true;
    }
    return (value & 0xFF00u) == 0x5800u;
}

char ImmediateRegister(uint8_t op)
{
    switch (op) {
    case 0xA9: return 'A';
    case 0xA2: return 'X';
    case 0xA0: return 'Y';
    default: return '?';
    }
}

unsigned NearestHudRoutineAddress(unsigned instructionAddress)
{
    constexpr std::array<unsigned, 6> routines = {{ 0x80C729, 0x80C73E, 0x80C747, 0x80C74F, 0x80C75F, 0x80C76F }};
    unsigned nearest = 0;
    for (const unsigned routine : routines) {
        if (routine <= instructionAddress) {
            nearest = routine;
        }
    }
    return nearest;
}

std::vector<HudScannedPosition> BuildScannedHudPositions(const SC4Core& core)
{
    std::vector<HudScannedPosition> positions;
    constexpr unsigned scanStart = 0x80C6D0;
    constexpr unsigned scanEnd = 0x80C790;
    unsigned startPc = SNESCore::snes2pc(static_cast<int>(scanStart));
    unsigned endPc = SNESCore::snes2pc(static_cast<int>(scanEnd));
    if (!IsValidPcOffset(core, startPc, 3)) {
        return positions;
    }
    endPc = (std::min)(endPc, static_cast<unsigned>(core.romSize));

    for (unsigned pc = startPc; pc + 2 < endPc; ++pc) {
        const uint8_t op = core.rom[pc];
        if (op != 0xA9 && op != 0xA2 && op != 0xA0) {
            continue;
        }

        const unsigned instructionAddress = SNESCore::pc2snes(static_cast<int>(pc));
        const uint16_t value = static_cast<uint16_t>(core.rom[pc + 1] | (core.rom[pc + 2] << 8));
        if (!IsLikelyHudPosition(op, value)) {
            continue;
        }
        HudScannedPosition position = {};
        position.label = HudPositionLabel(instructionAddress);
        position.valueAddress = SNESCore::pc2snes(static_cast<int>(pc + 1));
        position.routineAddress = NearestHudRoutineAddress(instructionAddress);
        position.value = value;
        position.reg = ImmediateRegister(op);
        position.tileOffset = (value & 0xFF00u) == 0x5800u;
        if (position.tileOffset) {
            const int offset = value & 0xFF;
            position.x = offset & 0x1F;
            position.y = (offset >> 5) - 1;
        } else {
            position.x = (value >> 8) & 0xFF;
            position.y = value & 0xFF;
        }
        positions.push_back(position);
    }

    return positions;
}

void DrawWordField(EditorState& state, const HudWordField& field)
{
    const unsigned currentValue = state.session.ReadRom(field.address, 2) & 0xFFFFu;
    int value = static_cast<int>(currentValue);

    ImGui::PushID(field.address);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(field.label);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputInt("##value", &value, 0, 0)) {
        value = std::clamp(value, 0, 0xFFFF);
        if (static_cast<unsigned>(value) != currentValue) {
            PushUndo(state);
            state.session.WriteRom(field.address, 2, static_cast<unsigned>(value));
            state.levelRenderer.Invalidate();
        }
    }

    ImGui::TableNextColumn();
    ImGui::Text("$%04X", currentValue);

    ImGui::TableNextColumn();
    ImGui::Text("$%06X", field.address);

    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", field.note);
    ImGui::PopID();
}

void DrawScannedPositionField(EditorState& state, const HudScannedPosition& field)
{
    int x = field.x;
    int y = field.y;
    const int originalX = x;
    const int originalY = y;

    ImGui::PushID(field.valueAddress);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(field.label);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool xChanged = ImGui::InputInt("##x", &x, 0, 0);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool yChanged = ImGui::InputInt("##y", &y, 0, 0);

    if (xChanged || yChanged) {
        x = field.tileOffset ? std::clamp(x, 0, 31) : std::clamp(x, 0, 0xFF);
        y = field.tileOffset ? std::clamp(y, 0, 6) : std::clamp(y, 0, 0xFF);
        if (x != originalX || y != originalY) {
            const unsigned newValue = field.tileOffset
                ? ((field.value & 0xFF00u) | static_cast<unsigned>(((y + 1) << 5) | x))
                : ((static_cast<unsigned>(x) << 8) | static_cast<unsigned>(y));
            PushUndo(state);
            state.session.WriteRom(field.valueAddress, 2, newValue);
            state.levelRenderer.Invalidate();
        }
    }

    ImGui::TableNextColumn();
    ImGui::Text("$%04X", field.value);

    ImGui::TableNextColumn();
    ImGui::Text("%c", field.reg);

    ImGui::TableNextColumn();
    if (field.tileOffset) {
        ImGui::Text("$%02X", field.value >> 8);
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableNextColumn();
    ImGui::Text("$%06X", field.valueAddress);

    ImGui::TableNextColumn();
    if (field.routineAddress) {
        ImGui::Text("$%06X", field.routineAddress);
    } else {
        ImGui::TextDisabled("setup");
    }
    ImGui::PopID();
}

void DrawPositionField(EditorState& state, const HudPositionField& field)
{
    const unsigned currentValue = state.session.ReadRom(field.address, 2) & 0xFFFFu;
    int x = field.tileOffset ? static_cast<int>(currentValue & 0x1Fu) : static_cast<int>((currentValue >> 8) & 0xFFu);
    int y = field.tileOffset ? static_cast<int>(((currentValue & 0xFFu) >> 5) - 1) : static_cast<int>(currentValue & 0xFFu);
    const int originalX = x;
    const int originalY = y;

    ImGui::PushID(field.address);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(field.label);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool xChanged = ImGui::InputInt("##x", &x, 0, 0);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool yChanged = ImGui::InputInt("##y", &y, 0, 0);

    if (xChanged || yChanged) {
        x = field.tileOffset ? std::clamp(x, 0, 31) : std::clamp(x, 0, 0xFF);
        y = field.tileOffset ? std::clamp(y, 0, 6) : std::clamp(y, 0, 0xFF);
        if (x != originalX || y != originalY) {
            const unsigned newValue = field.tileOffset
                ? ((currentValue & 0xFF00u) | static_cast<unsigned>(((y + 1) << 5) | x))
                : ((static_cast<unsigned>(x) << 8) | static_cast<unsigned>(y));
            PushUndo(state);
            state.session.WriteRom(field.address, 2, newValue);
            state.levelRenderer.Invalidate();
        }
    }

    ImGui::TableNextColumn();
    ImGui::Text("$%04X", currentValue);

    ImGui::TableNextColumn();
    ImGui::Text("$%06X", field.address);

    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", field.note);
    ImGui::PopID();
}

template <size_t N>
void DrawWordSection(EditorState& state, const char* title, const std::array<HudWordField, N>& fields)
{
    ImGui::SeparatorText(title);
    if (!ImGui::BeginTable(title, 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableSetupColumn("From text.asm");
    ImGui::TableHeadersRow();

    for (const HudWordField& field : fields) {
        DrawWordField(state, field);
    }

    ImGui::EndTable();
}

void DrawScannedPositionSection(EditorState& state)
{
    const std::vector<HudScannedPosition> positions = BuildScannedHudPositions(state.session.Core());

    ImGui::SeparatorText("HUD update positions");
    ImGui::TextDisabled("Scans A/X/Y immediate operands. $58xx rows edit the low-byte tilemap offset while preserving the $58 HUD page.");

    if (positions.empty()) {
        ImGui::TextUnformatted("No HUD position operands were found in this ROM.");
        return;
    }

    if (!ImGui::BeginTable("HudScannedPositions", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("Reg", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("Page", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableSetupColumn("Routine", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableHeadersRow();

    for (const HudScannedPosition& position : positions) {
        DrawScannedPositionField(state, position);
    }

    ImGui::EndTable();
}

void DrawPositionSection(EditorState& state)
{
    ImGui::SeparatorText("Positions");
    if (!ImGui::BeginTable("Positions", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableSetupColumn("From text.asm");
    ImGui::TableHeadersRow();

    for (const HudPositionField& field : kHudPositionFields) {
        DrawPositionField(state, field);
    }

    ImGui::EndTable();
}

void DrawStaticTextPositionSection(EditorState& state)
{
    ImGui::SeparatorText("Static HUD text positions");
    ImGui::TextDisabled("From text.asm HUD_Construct00/HUD_Construct01 text blocks. $58xx values are shown as visible HUD tile X/Y.");
    if (!ImGui::BeginTable("StaticHudTextPositions", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableSetupColumn("From text.asm");
    ImGui::TableHeadersRow();

    for (const HudPositionField& field : kHudStaticTextFields) {
        DrawPositionField(state, field);
    }

    ImGui::EndTable();
}

} // namespace

void DrawHudEditor(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Load a ROM first.");
        return;
    }

    ImGui::BeginChild("hud-textasm-fields", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextUnformatted("HUD words from text.asm");
    ImGui::TextDisabled("Values are patched as 16-bit little-endian ROM words at the shown SNES address.");

    DrawScannedPositionSection(state);
    DrawStaticTextPositionSection(state);
    DrawPositionSection(state);
    DrawWordSection(state, "Counters", kHudCounterFields);
    DrawWordSection(state, "HUD pointers", kHudPointerFields);
    DrawWordSection(state, "HUD update table", kHudUpdateTableFields);
    DrawWordSection(state, "Subweapon upgrade HUD sprite PPU", kSubweaponUpgradeFields);

    ImGui::EndChild();
}
