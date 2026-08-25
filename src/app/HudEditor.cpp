#include "HudEditor.h"

#include "EditorState.h"
#include "EditorUndo.h"

#include "SC4Core.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <string>
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

enum class HudPreviewKind {
    ScoreText,
    PlayerText,
    BossText,
    TimerText,
    BlockText,
    LivesText,
    Score,
    PlayerHealth,
    BossHealth,
    Timer,
    Hearts,
    Lives,
};

struct HudCanvasElement {
    const char* label;
    unsigned address;
    uint16_t defaultValue;
    int width;
    HudPreviewKind kind;
};

constexpr std::array<HudCanvasElement, 12> kHudCanvasElements = {{
    { "Score text", 0x81A3BF, 0x5821, 40, HudPreviewKind::ScoreText },
    { "Player text", 0x81A3E4, 0x5842, 48, HudPreviewKind::PlayerText },
    { "Enemy text", 0x81A402, 0x5863, 40, HudPreviewKind::BossText },
    { "Time row", 0x81A40A, 0x5870, 72, HudPreviewKind::TimerText },
    { "Block row", 0x81A3CB, 0x582E, 96, HudPreviewKind::BlockText },
    { "Lives text", 0x81A3E0, 0x583D, 16, HudPreviewKind::LivesText },
    { "Score value", 0x80C72A, 0x5827, 64, HudPreviewKind::Score },
    { "Player health bar", 0x80C73F, 0x5848, 60, HudPreviewKind::PlayerHealth },
    { "Boss health bar", 0x80C748, 0x5868, 60, HudPreviewKind::BossHealth },
    { "Timer value", 0x80C750, 0x587A, 24, HudPreviewKind::Timer },
    { "Heart value", 0x80C760, 0x5857, 16, HudPreviewKind::Hearts },
    { "Lives value", 0x80C770, 0x585C, 16, HudPreviewKind::Lives },
}};

int g_selectedHudElement = 0;
int g_draggedHudElement = -1;
bool g_hudDragUndoActive = false;
ImVec2 g_hudDragOffset = ImVec2(0.0f, 0.0f);
std::string g_hudSaveStatus;

constexpr unsigned kScorePrefixAddress = 0x81A3CD;
constexpr uint8_t kScorePrefixTile = 0x01;

constexpr std::array<HudPositionField, 3> kHudPositionFields = {{
    { "Subweapon Y", 0x80C6D8, "LDY #$081C from $80C6D7", false },
    { "Heart position", 0x80C760, "LDY #$5859 from $80C75F", true },
    { "Multishot X/Y", 0x80C6F8, "LDY #$0040 from $80C6F7", false },
}};

constexpr std::array<HudPositionField, 6> kHudStaticTextFields = {{
    { "Score text", 0x81A3BF, "Static SCORE text destination", true },
    { "Block row", 0x81A3CB, "Static score-prefix/BLOCK row destination", true },
    { "Lives text", 0x81A3E0, "Static P- text destination", true },
    { "Player text", 0x81A3E4, "Static PLAYER text destination", true },
    { "Enemy text", 0x81A402, "Static ENEMY text destination", true },
    { "Time row", 0x81A40A, "Static item-border/TIME row destination", true },
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

ImVec2 HudElementPosition(uint16_t value, const HudCanvasElement& element)
{
    (void)element;
    const int offset = std::clamp(static_cast<int>(value) - 0x5800, 0, 0x3FF);
    const int x = offset & 0x1F;
    const int y = offset >> 5;
    return ImVec2(
        static_cast<float>(x * 8),
        static_cast<float>(y * 8));
}

uint16_t HudPositionValue(uint16_t currentValue, const HudCanvasElement& element, int x, int y)
{
    (void)currentValue;
    (void)element;
    const int tileX = std::clamp(x / 8, 0, 31);
    const int tileY = std::clamp(y / 8, 0, 27);
    return static_cast<uint16_t>(0x5800 + (tileY << 5) + tileX);
}

void DrawHudHealthBar(ImDrawList* drawList, ImVec2 pos, ImU32 color)
{
    for (int i = 0; i < 12; ++i) {
        const ImVec2 barMin(pos.x + i * 5.0f, pos.y + 1.0f);
        drawList->AddRectFilled(barMin, ImVec2(barMin.x + 3.0f, barMin.y + 6.0f), color);
    }
}

void DrawHudElementPreview(ImDrawList* drawList, ImVec2 pos, const HudCanvasElement& element)
{
    constexpr ImU32 green = IM_COL32(112, 232, 112, 255);
    constexpr ImU32 orange = IM_COL32(255, 184, 64, 255);
    constexpr ImU32 red = IM_COL32(216, 32, 48, 255);
    constexpr ImU32 gray = IM_COL32(208, 208, 208, 255);
    ImFont* font = ImGui::GetFont();
    constexpr float fontSize = 8.0f;

    switch (element.kind) {
    case HudPreviewKind::ScoreText:
        drawList->AddText(font, fontSize, pos, green, "SCORE");
        break;
    case HudPreviewKind::PlayerText:
        drawList->AddText(font, fontSize, pos, green, "PLAYER");
        break;
    case HudPreviewKind::BossText:
        drawList->AddText(font, fontSize, pos, green, "ENEMY");
        break;
    case HudPreviewKind::TimerText:
        drawList->AddLine(ImVec2(pos.x, pos.y + 1.0f), ImVec2(pos.x + 32.0f, pos.y + 1.0f), gray);
        drawList->AddText(font, fontSize, ImVec2(pos.x + 40.0f, pos.y), green, "TIME");
        break;
    case HudPreviewKind::BlockText:
        drawList->AddLine(ImVec2(pos.x + 16.0f, pos.y + 7.0f), ImVec2(pos.x + 48.0f, pos.y + 7.0f), gray);
        drawList->AddText(font, fontSize, ImVec2(pos.x + 56.0f, pos.y), green, "BLOCK");
        break;
    case HudPreviewKind::LivesText:
        drawList->AddText(font, fontSize, pos, gray, "P-");
        break;
    case HudPreviewKind::Score:
        drawList->AddText(font, fontSize, pos, orange, "00001350");
        break;
    case HudPreviewKind::PlayerHealth:
        DrawHudHealthBar(drawList, pos, red);
        break;
    case HudPreviewKind::BossHealth:
        DrawHudHealthBar(drawList, pos, red);
        break;
    case HudPreviewKind::Timer:
        drawList->AddText(font, fontSize, pos, orange, "202");
        break;
    case HudPreviewKind::Hearts:
        drawList->AddText(font, fontSize, pos, orange, "12");
        break;
    case HudPreviewKind::Lives:
        drawList->AddText(font, fontSize, pos, gray, "04");
        break;
    }
}

std::string HudCurrentTime()
{
    std::time_t now = std::time(nullptr);
    std::tm localTime = {};
    localtime_s(&localTime, &now);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

void DrawHudCanvas(EditorState& state)
{
    if (ImGui::Button("Save ROM")) {
        if (state.session.Save()) {
            g_hudSaveStatus = "Saved " + state.session.Info().path + " at " + HudCurrentTime();
        } else {
            g_hudSaveStatus = "Save failed: " + state.session.LastError();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset positions")) {
        bool changed = false;
        for (const HudCanvasElement& element : kHudCanvasElements) {
            changed |= (state.session.ReadRom(element.address, 2) & 0xFFFFu) != element.defaultValue;
        }
        changed |= state.session.ReadRom(kScorePrefixAddress, 1) != kScorePrefixTile;
        changed |= state.session.ReadRom(kScorePrefixAddress + 1, 1) != kScorePrefixTile;
        if (changed) {
            PushUndo(state);
            for (const HudCanvasElement& element : kHudCanvasElements) {
                state.session.WriteRom(element.address, 2, element.defaultValue);
            }
            state.session.WriteRom(kScorePrefixAddress, 1, kScorePrefixTile);
            state.session.WriteRom(kScorePrefixAddress + 1, 1, kScorePrefixTile);
            g_hudSaveStatus = "HUD positions reset. Save ROM to keep them.";
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("8x8 snap");
    if (!g_hudSaveStatus.empty()) {
        ImGui::TextWrapped("%s", g_hudSaveStatus.c_str());
    }

    const ImVec2 canvasSize(256.0f, 224.0f);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (availableWidth > canvasSize.x) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - canvasSize.x) * 0.5f);
    }
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    ImGui::InvisibleButton("hud-canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(0, 0, 0, 255));
    drawList->AddRect(canvasMin, canvasMax, IM_COL32(96, 104, 112, 255));
    drawList->PushClipRect(canvasMin, canvasMax, true);

    // The middle of the item frame is a separate fixed construction row. Its
    // top and bottom edges belong to the draggable BLOCK and TIME rows.
    const ImVec2 itemMin(canvasMin.x + 128.0f, canvasMin.y);
    const ImVec2 itemMax(itemMin.x + 32.0f, itemMin.y + 28.0f);
    drawList->AddRectFilled(ImVec2(itemMin.x, itemMin.y + 8.0f), ImVec2(itemMax.x, itemMax.y - 4.0f), IM_COL32(24, 28, 36, 255));
    drawList->AddLine(ImVec2(itemMin.x, itemMin.y + 8.0f), ImVec2(itemMin.x, itemMax.y - 4.0f), IM_COL32(128, 144, 168, 255), 2.0f);
    drawList->AddLine(ImVec2(itemMax.x, itemMin.y + 8.0f), ImVec2(itemMax.x, itemMax.y - 4.0f), IM_COL32(128, 144, 168, 255), 2.0f);
    drawList->AddText(ImGui::GetFont(), 8.0f, ImVec2(itemMin.x + 7.0f, itemMin.y + 10.0f), IM_COL32(232, 232, 232, 255), "ITEM");

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool canvasHovered = ImGui::IsItemHovered();
    int hoveredElement = -1;
    std::array<ImVec2, kHudCanvasElements.size()> positions = {};
    for (int i = static_cast<int>(kHudCanvasElements.size()) - 1; i >= 0; --i) {
        const HudCanvasElement& element = kHudCanvasElements[static_cast<size_t>(i)];
        const uint16_t value = static_cast<uint16_t>(state.session.ReadRom(element.address, 2));
        positions[static_cast<size_t>(i)] = HudElementPosition(value, element);
        const ImVec2 elementMin(canvasMin.x + positions[static_cast<size_t>(i)].x, canvasMin.y + positions[static_cast<size_t>(i)].y);
        const ImVec2 elementMax(elementMin.x + static_cast<float>(element.width), elementMin.y + 8.0f);
        if (canvasHovered && mouse.x >= elementMin.x && mouse.x < elementMax.x && mouse.y >= elementMin.y && mouse.y < elementMax.y) {
            hoveredElement = i;
            break;
        }
    }

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredElement >= 0) {
        g_selectedHudElement = hoveredElement;
        g_draggedHudElement = hoveredElement;
        g_hudDragUndoActive = false;
        const ImVec2 pos = positions[static_cast<size_t>(hoveredElement)];
        g_hudDragOffset = ImVec2(mouse.x - canvasMin.x - pos.x, mouse.y - canvasMin.y - pos.y);
    }

    if (g_draggedHudElement >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const HudCanvasElement& element = kHudCanvasElements[static_cast<size_t>(g_draggedHudElement)];
        const uint16_t currentValue = static_cast<uint16_t>(state.session.ReadRom(element.address, 2));
        int x = static_cast<int>(mouse.x - canvasMin.x - g_hudDragOffset.x);
        int y = static_cast<int>(mouse.y - canvasMin.y - g_hudDragOffset.y);
        x = std::clamp(((x + 4) / 8) * 8, 0, 248);
        y = std::clamp(((y + 4) / 8) * 8, 0, 216);
        const uint16_t newValue = HudPositionValue(currentValue, element, x, y);
        if (newValue != currentValue) {
            if (!g_hudDragUndoActive) {
                PushUndo(state);
                g_hudDragUndoActive = true;
            }
            state.session.WriteRom(element.address, 2, newValue);
            if (element.kind == HudPreviewKind::Score) {
                const unsigned prefixTile = newValue == element.defaultValue ? kScorePrefixTile : 0;
                state.session.WriteRom(kScorePrefixAddress, 1, prefixTile);
                state.session.WriteRom(kScorePrefixAddress + 1, 1, prefixTile);
            }
            g_hudSaveStatus = std::string(element.label) + " moved. Save ROM to keep it.";
        }
    }
    if (g_draggedHudElement >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        g_draggedHudElement = -1;
        g_hudDragUndoActive = false;
    }

    for (int i = 0; i < static_cast<int>(kHudCanvasElements.size()); ++i) {
        const HudCanvasElement& element = kHudCanvasElements[static_cast<size_t>(i)];
        const uint16_t value = static_cast<uint16_t>(state.session.ReadRom(element.address, 2));
        const ImVec2 position = HudElementPosition(value, element);
        const ImVec2 elementMin(canvasMin.x + position.x, canvasMin.y + position.y);
        const ImVec2 elementMax(elementMin.x + static_cast<float>(element.width), elementMin.y + 8.0f);
        DrawHudElementPreview(drawList, elementMin, element);
        if (i == g_selectedHudElement) {
            drawList->AddRect(elementMin, elementMax, IM_COL32(255, 220, 64, 255));
        } else if (i == hoveredElement) {
            drawList->AddRect(elementMin, elementMax, IM_COL32(224, 224, 224, 255));
        }
    }
    drawList->PopClipRect();

    const HudCanvasElement& selected = kHudCanvasElements[static_cast<size_t>(std::clamp(g_selectedHudElement, 0, static_cast<int>(kHudCanvasElements.size()) - 1))];
    const uint16_t selectedValue = static_cast<uint16_t>(state.session.ReadRom(selected.address, 2));
    const ImVec2 selectedPosition = HudElementPosition(selectedValue, selected);
    ImGui::Text("Selected: %s   X %d   Y %d", selected.label, static_cast<int>(selectedPosition.x), static_cast<int>(selectedPosition.y));
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
    return value >= 0x5800u && value <= 0x5BFFu;
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
        position.tileOffset = value >= 0x5800u && value <= 0x5BFFu;
        if (position.tileOffset) {
            const int offset = value - 0x5800;
            position.x = offset & 0x1F;
            position.y = (offset >> 5) & 0x1F;
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
        y = field.tileOffset ? std::clamp(y, 0, 27) : std::clamp(y, 0, 0xFF);
        if (x != originalX || y != originalY) {
            const unsigned newValue = field.tileOffset
                ? (0x5800u + static_cast<unsigned>((y << 5) | x))
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
    const int tileOffset = field.tileOffset ? static_cast<int>(currentValue) - 0x5800 : 0;
    int x = field.tileOffset ? (tileOffset & 0x1F) : static_cast<int>((currentValue >> 8) & 0xFFu);
    int y = field.tileOffset ? ((tileOffset >> 5) & 0x1F) : static_cast<int>(currentValue & 0xFFu);
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
        y = field.tileOffset ? std::clamp(y, 0, 27) : std::clamp(y, 0, 0xFF);
        if (x != originalX || y != originalY) {
            const unsigned newValue = field.tileOffset
                ? (0x5800u + static_cast<unsigned>((y << 5) | x))
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
    DrawHudCanvas(state);

    ImGui::SeparatorText("ROM fields");
    DrawScannedPositionSection(state);
    DrawStaticTextPositionSection(state);
    DrawPositionSection(state);
    DrawWordSection(state, "Counters", kHudCounterFields);
    DrawWordSection(state, "HUD pointers", kHudPointerFields);
    DrawWordSection(state, "HUD update table", kHudUpdateTableFields);
    DrawWordSection(state, "Subweapon upgrade HUD sprite PPU", kSubweaponUpgradeFields);

    ImGui::EndChild();
}
