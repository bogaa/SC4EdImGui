#include "PlayerCharacterEditor.h"

#include "EditorState.h"
#include "EditorUndo.h"
#include "SC4Core.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace {

constexpr unsigned kSimonFrameTable = 0x848246;
constexpr int kFrameCount = 115;
constexpr int kSlotsPerFrame = 8;
constexpr int kCharacterTileCount = 448;
constexpr int kSheetColumns = 8;
constexpr int kSheetRows = kCharacterTileCount / kSheetColumns;
constexpr unsigned kIndependentLayoutHeader = 0xFE8F20;
constexpr unsigned kIndependentLayoutHookRoutine = 0xFE8F30;
constexpr unsigned kIndependentLayoutPointerTable = 0xFE8F70;
constexpr unsigned kIndependentLayoutRecords = 0xFE9060;
constexpr unsigned kIndependentLayoutRecordSize = 1 + kSlotsPerFrame * 4;
constexpr unsigned kSpriteRendererHook = 0x808E58;
constexpr std::array<unsigned char, 8> kIndependentLayoutMagic = {'S', 'C', '4', 'P', 'C', 'X', 'Y', '1'};

int g_selectedFrame = 0;
int g_selectedSlot = 0;
int g_characterPalette = 0;
int g_sheetZoom = 1;
bool g_scrollSheetToSelection = true;
std::string g_layoutStatus;

constexpr std::array<const char*, kFrameCount> kFrameNames = {
    "Simon Walk 1", "Simon Walk 2", "Simon Walk 3", "Simon Walk 4", "Simon Walk 5", "Simon Walk 6",
    "Crouch 1", "Crouch 2", "Crouch 3", "Crouch 4", "Crouch 5", "Crouch 6",
    "Whip Windup 1", "Whip Windup 2", "Whip", "Whip Diagonal Up Windup", "Whip Diagonal Up", "Whip Up",
    "Air Whip Down Windup 1", "Air Whip Down Windup 2", "Air Whip Down", "Air Whip Diagonal Down",
    "Limp Down", "Limp Diagonal Down", "Limp Right", "Limp Diagonal Up", "Limp Up",
    "Whip Crouch Windup 1", "Whip Crouch Windup 2", "Whip Crouch",
    "Limp Crouch Down", "Limp Crouch Diagonal Down", "Limp Crouch Right", "Limp Crouch Diagonal Up", "Limp Crouch Up",
    "Swing Right", "Swing Right Down", "Swing Down", "Swing Left Down", "Swing Left",
    "Stair Up 1", "Stair Up 2", "Stair Up 3", "Stair Up 4", "Stair Up 5", "Stair Up 6",
    "Stair Down 1", "Stair Down 2", "Stair Down 3", "Stair Down 4", "Stair Down 5", "Stair Down 6",
    "Stairs Up Whip Windup 1", "Stairs Up Whip Windup 2", "Stairs Up Whip",
    "Stairs Down Whip Windup 1", "Stairs Down Whip Windup 2", "Stairs Down Whip",
    "Stairs Up Diagonal Windup 1", "Stairs Up Diagonal Up", "Stairs Up Whip Up",
    "Stairs Down Diagonal Windup 1", "Stairs Down Diagonal Windup 2", "Stairs Down Whip Diagonal",
    "Stairs Up Limp Down", "Stairs Up Limp Diagonal Down", "Stairs Up Limp Right", "Stairs Up Limp Diagonal Up", "Stairs Up Limp Up",
    "Stairs Down Limp Up", "Stairs Down Limp Diagonal Up", "Stairs Down Limp Right", "Stairs Down Limp Diagonal Down", "Stairs Down Limp Down",
    "Air Whip Windup 1", "Air Whip Windup 2", "Air Whip Up", "Hurt", "Death 1", "Death 2", "Death 3",
    "Swing Right Throw 1", "Swing Right Throw 2", "Swing Right Throw 3",
    "Swing Right Down Throw 1", "Swing Right Down Throw 2", "Swing Right Down Throw 3",
    "Swing Down Throw 1", "Swing Down Throw 2", "Swing Down Throw 3",
    "Swing Left Down Throw 1", "Swing Left Down Throw 2", "Swing Left Down Throw 3",
    "Swing Left Throw 1", "Swing Left Throw 2", "Swing Left Throw 3",
    "Limp Idle", "Limp Crouch Idle", "Stairs Up Limp Idle", "Stairs Down Limp Idle",
    "Air", "Air Windup", "Air Whip", "Air Limp Down", "Air Limp Diagonal Down", "Air Limp Forward", "Air Limp Diagonal Up", "Air Limp Up",
    "Air Whip Diagonal Up", "Air Whip Up 2", "Air Whip Up Windup 1", "Air Whip Diagonal Up Windup 2", "Air Limp Idle",
    "Idle", "Idle Near Hole"
};

int DecodeCharacterTile(unsigned offset)
{
    const unsigned row = offset / 0x400;
    const unsigned withinRow = offset % 0x400;
    if ((withinRow % 0x40) != 0 || withinRow / 0x40 >= 8) {
        return -1;
    }
    const unsigned tile = row * 8 + withinRow / 0x40;
    return tile < kCharacterTileCount ? static_cast<int>(tile) : -1;
}

unsigned EncodeCharacterTile(int tile)
{
    tile = std::clamp(tile, 0, kCharacterTileCount - 1);
    return static_cast<unsigned>(tile / 8) * 0x400u + static_cast<unsigned>(tile % 8) * 0x40u;
}

int CharacterTilePixel(const SC4Core& core, int tile, int x, int y)
{
    if (tile < 0 || tile >= kCharacterTileCount || x < 0 || x >= 16 || y < 0 || y >= 16) {
        return 0;
    }
    const int tileColumn = tile % kSheetColumns;
    const int tileRow = tile / kSheetColumns;
    const unsigned tile8 = static_cast<unsigned>(tileRow * 32 + tileColumn * 2 + (x / 8) + (y / 8) * 16);
    const unsigned offset = 0x10000u + tile8 * 0x20u;
    if (offset + 0x20u > sizeof(core.ram)) {
        return 0;
    }
    const BYTE* source = core.ram + offset;
    const int pixelX = x & 7;
    const int pixelY = y & 7;
    const BYTE mask = static_cast<BYTE>(0x80u >> pixelX);
    return ((source[pixelY * 2] & mask) ? 1 : 0) |
        ((source[pixelY * 2 + 1] & mask) ? 2 : 0) |
        ((source[16 + pixelY * 2] & mask) ? 4 : 0) |
        ((source[16 + pixelY * 2 + 1] & mask) ? 8 : 0);
}

ImU32 CharacterColor(const SC4Core& core, int palette, int index)
{
    const uint16_t color = core.palCache[0x80 | ((palette & 7) << 4) | (index & 15)];
    const ImU8 red = static_cast<ImU8>(((color >> 10) & 31) * 255 / 31);
    const ImU8 green = static_cast<ImU8>(((color >> 5) & 31) * 255 / 31);
    const ImU8 blue = static_cast<ImU8>((color & 31) * 255 / 31);
    return IM_COL32(red, green, blue, 255);
}

void DrawCharacterTile(const SC4Core& core, ImDrawList* drawList, ImVec2 position, int tile,
    float scale, int palette, bool flipX = false, bool flipY = false)
{
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int sourceX = flipX ? 15 - x : x;
            const int sourceY = flipY ? 15 - y : y;
            const int pixel = CharacterTilePixel(core, tile, sourceX, sourceY);
            if (!pixel) continue;
            const ImVec2 pixelMin(position.x + x * scale, position.y + y * scale);
            drawList->AddRectFilled(pixelMin, ImVec2(pixelMin.x + scale, pixelMin.y + scale),
                CharacterColor(core, palette, pixel));
        }
    }
}

unsigned OriginalFrameAssemblyAddress(const RomSession& session, int frame)
{
    if (frame >= 35 && frame <= 39) {
        return 0x840000u | session.ReadRom(0x81909Fu + static_cast<unsigned>(frame - 35) * 2u, 2);
    }
    if (frame >= 81 && frame <= 95) {
        return 0x840000u | session.ReadRom(0x8191A1u + static_cast<unsigned>(frame - 81) * 2u, 2);
    }
    return 0x849C25u;
}

bool IndependentLayoutsInstalled(const RomSession& session)
{
    if (!session.IsExpandedRom()) return false;
    for (size_t i = 0; i < kIndependentLayoutMagic.size(); ++i) {
        if (session.ReadRom(kIndependentLayoutHeader + static_cast<unsigned>(i), 1) != kIndependentLayoutMagic[i]) {
            return false;
        }
    }
    return true;
}

unsigned IndependentFrameAssemblyAddress(const RomSession& session, int frame)
{
    if (!IndependentLayoutsInstalled(session) || frame < 0 || frame >= kFrameCount) return 0;
    const unsigned pointer = session.ReadRom(
        kIndependentLayoutPointerTable + static_cast<unsigned>(frame) * 2u, 2);
    return pointer ? 0xFE0000u | pointer : 0;
}

unsigned FrameAssemblyAddress(const RomSession& session, int frame)
{
    const unsigned independent = IndependentFrameAssemblyAddress(session, frame);
    return independent ? independent : OriginalFrameAssemblyAddress(session, frame);
}

void WriteBytes(RomSession& session, unsigned address, const unsigned char* bytes, size_t byteCount)
{
    for (size_t i = 0; i < byteCount; ++i) {
        session.WriteRom(address + static_cast<unsigned>(i), 1, bytes[i]);
    }
}

bool InstallIndependentLayouts(EditorState& state)
{
    if (!state.session.IsExpandedRom()) {
        g_layoutStatus = "Expand the ROM before enabling independent frame positions.";
        return false;
    }
    if (IndependentLayoutsInstalled(state.session)) return true;

    const unsigned reservedEnd = kIndependentLayoutRecords + kFrameCount * kIndependentLayoutRecordSize;
    for (unsigned address = kIndependentLayoutHeader; address < reservedEnd; ++address) {
        const unsigned value = state.session.ReadRom(address, 1);
        if (value != 0x00u && value != 0xFFu) {
            g_layoutStatus = "Expanded bank FE is already in use; independent layouts were not installed.";
            return false;
        }
    }
    if (state.session.ReadRom(kSpriteRendererHook, 1) != 0x86u ||
        state.session.ReadRom(kSpriteRendererHook + 1, 1) != 0xFCu ||
        state.session.ReadRom(kSpriteRendererHook + 2, 1) != 0xB4u ||
        state.session.ReadRom(kSpriteRendererHook + 3, 1) != 0x00u) {
        g_layoutStatus = "The sprite renderer is already modified; independent layouts were not installed.";
        return false;
    }

    RomUndoSnapshot snapshot = state.session.CreateUndoSnapshot(state.selectedEventIndex);
    WriteBytes(state.session, kIndependentLayoutHeader,
        kIndependentLayoutMagic.data(), kIndependentLayoutMagic.size());
    constexpr std::array<unsigned char, 50> routine = {
        0x86, 0xFC,                         // STX $FC
        0xE0, 0x40, 0x05,                   // CPX #$0540
        0xD0, 0x20,                         // BNE defaultBank
        0xAD, 0x60, 0x05,                   // LDA $0560
        0xF0, 0x1B,                         // BEQ defaultBank
        0x3A,                               // DEC
        0xC9, 0x73, 0x00,                   // CMP #$0073
        0xB0, 0x15,                         // BCS defaultBank
        0x0A, 0xDA, 0xAA,                   // ASL / PHX / TAX
        0xBF, 0x70, 0x8F, 0xFE,             // LDA.l $FE8F70,X
        0xFA, 0xF0, 0x0B,                   // PLX / BEQ defaultBank
        0xA8, 0xE2, 0x20,                   // TAY / SEP #$20
        0xA9, 0xFE, 0x48, 0xAB,             // LDA #$FE / PHA / PLB
        0xC2, 0x20, 0x98, 0x6B,             // REP #$20 / TYA / RTL
        0xE2, 0x20, 0xA9, 0x04, 0x48, 0xAB, // defaultBank: DBR = $04
        0xC2, 0x20, 0xB4, 0x00, 0x6B        // REP #$20 / LDY $00,X / RTL
    };
    WriteBytes(state.session, kIndependentLayoutHookRoutine, routine.data(), routine.size());
    for (int frame = 0; frame < kFrameCount; ++frame) {
        state.session.WriteRom(kIndependentLayoutPointerTable + static_cast<unsigned>(frame) * 2u, 2, 0);
    }
    state.session.WriteRom(kSpriteRendererHook, 1, 0x22);
    state.session.WriteRom(kSpriteRendererHook + 1, 1, kIndependentLayoutHookRoutine & 0xFFu);
    state.session.WriteRom(kSpriteRendererHook + 2, 1, (kIndependentLayoutHookRoutine >> 8) & 0xFFu);
    state.session.WriteRom(kSpriteRendererHook + 3, 1, (kIndependentLayoutHookRoutine >> 16) & 0xFFu);
    CommitUndoSnapshot(state, std::move(snapshot));
    g_layoutStatus = "Independent frame-position support installed.";
    return true;
}

bool MakeFrameIndependent(EditorState& state, int frame)
{
    if (!InstallIndependentLayouts(state)) return false;
    if (IndependentFrameAssemblyAddress(state.session, frame)) return true;

    RomUndoSnapshot snapshot = state.session.CreateUndoSnapshot(state.selectedEventIndex);
    const unsigned source = OriginalFrameAssemblyAddress(state.session, frame);
    const unsigned destination = kIndependentLayoutRecords + static_cast<unsigned>(frame) * kIndependentLayoutRecordSize;
    const unsigned pieceCount = (std::min)(state.session.ReadRom(source, 1), static_cast<unsigned>(kSlotsPerFrame));
    for (unsigned i = 0; i < kIndependentLayoutRecordSize; ++i) {
        state.session.WriteRom(destination + i, 1, 0);
    }
    for (unsigned i = 0; i < 1u + pieceCount * 4u; ++i) {
        state.session.WriteRom(destination + i, 1, state.session.ReadRom(source + i, 1));
    }
    state.session.WriteRom(kIndependentLayoutPointerTable + static_cast<unsigned>(frame) * 2u,
        2, destination & 0xFFFFu);
    CommitUndoSnapshot(state, std::move(snapshot));
    g_layoutStatus = "Frame " + std::to_string(frame + 1) + " now has independent positions.";
    return true;
}

void RestoreFrameSharedLayout(EditorState& state, int frame)
{
    if (!IndependentFrameAssemblyAddress(state.session, frame)) return;
    RomUndoSnapshot snapshot = state.session.CreateUndoSnapshot(state.selectedEventIndex);
    state.session.WriteRom(kIndependentLayoutPointerTable + static_cast<unsigned>(frame) * 2u, 2, 0);
    CommitUndoSnapshot(state, std::move(snapshot));
    g_layoutStatus = "Frame " + std::to_string(frame + 1) + " uses the original shared layout again.";
}

struct FramePiece {
    int x = 0;
    int y = 0;
    int slot = 0;
    bool flipX = false;
    bool flipY = false;
};

std::array<int, kSlotsPerFrame> ReadFrameTiles(const RomSession& session, int frame)
{
    std::array<int, kSlotsPerFrame> tiles{};
    for (int slot = 0; slot < kSlotsPerFrame; ++slot) {
        const unsigned address = kSimonFrameTable + static_cast<unsigned>(frame * kSlotsPerFrame + slot) * 2u;
        tiles[slot] = (std::max)(0, DecodeCharacterTile(session.ReadRom(address, 2)));
    }
    return tiles;
}

std::array<unsigned, kSlotsPerFrame> ReadFramePieceAddresses(const RomSession& session, int frame)
{
    std::array<unsigned, kSlotsPerFrame> addresses{};
    const unsigned assembly = FrameAssemblyAddress(session, frame);
    const int pieceCount = std::clamp(static_cast<int>(session.ReadRom(assembly, 1)), 0, kSlotsPerFrame);
    for (int pieceIndex = 0; pieceIndex < pieceCount; ++pieceIndex) {
        const unsigned address = assembly + 1u + static_cast<unsigned>(pieceIndex) * 4u;
        const unsigned map = session.ReadRom(address + 2, 2);
        const int slot = static_cast<int>((map & 0xFFu) / 2u);
        if (slot >= 0 && slot < kSlotsPerFrame) {
            addresses[slot] = address;
        }
    }
    return addresses;
}

void DrawCompiledFrame(EditorState& state)
{
    const SC4Core& core = state.session.Core();
    const auto tiles = ReadFrameTiles(state.session, g_selectedFrame);
    const unsigned assembly = FrameAssemblyAddress(state.session, g_selectedFrame);
    const int pieceCount = std::clamp(static_cast<int>(state.session.ReadRom(assembly, 1)), 0, kSlotsPerFrame);
    std::array<FramePiece, kSlotsPerFrame> pieces{};
    int minX = 0;
    int minY = 0;
    int maxX = 16;
    int maxY = 16;
    for (int pieceIndex = 0; pieceIndex < pieceCount; ++pieceIndex) {
        const unsigned address = assembly + 1u + static_cast<unsigned>(pieceIndex) * 4u;
        FramePiece& piece = pieces[pieceIndex];
        piece.x = static_cast<int8_t>(state.session.ReadRom(address, 1));
        piece.y = static_cast<int8_t>(state.session.ReadRom(address + 1, 1));
        const unsigned map = state.session.ReadRom(address + 2, 2);
        piece.slot = static_cast<int>((map & 0xFFu) / 2u);
        const unsigned attributes = map >> 8;
        piece.flipX = (attributes & 0x40u) != 0;
        piece.flipY = (attributes & 0x80u) != 0;
        minX = (std::min)(minX, piece.x);
        minY = (std::min)(minY, piece.y);
        maxX = (std::max)(maxX, piece.x + 16);
        maxY = (std::max)(maxY, piece.y + 16);
    }

    ImGui::Text("Frame %d  %s", g_selectedFrame + 1, kFrameNames[g_selectedFrame]);
    ImGui::TextDisabled("Assembly $%06X", assembly);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 boxSize((std::max)(180.0f, available.x), 142.0f);
    ImGui::InvisibleButton("##compiled-frame", boxSize);
    const ImVec2 boxMin = ImGui::GetItemRectMin();
    const ImVec2 boxMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(9, 9, 12, 255));
    drawList->AddRect(boxMin, boxMax, IM_COL32(75, 82, 88, 255));
    const float scale = (std::min)(4.0f, (std::min)((boxSize.x - 16.0f) / (maxX - minX),
        (boxSize.y - 16.0f) / (maxY - minY)));
    const ImVec2 origin(
        boxMin.x + (boxSize.x - (maxX - minX) * scale) * 0.5f - minX * scale,
        boxMin.y + (boxSize.y - (maxY - minY) * scale) * 0.5f - minY * scale);
    for (int pieceIndex = 0; pieceIndex < pieceCount; ++pieceIndex) {
        const FramePiece& piece = pieces[pieceIndex];
        if (piece.slot < 0 || piece.slot >= kSlotsPerFrame) continue;
        DrawCharacterTile(core, drawList,
            ImVec2(origin.x + piece.x * scale, origin.y + piece.y * scale),
            tiles[piece.slot], scale, g_characterPalette, piece.flipX, piece.flipY);
    }
}

void DrawSelectedFramePositions(EditorState& state)
{
    bool independent = IndependentFrameAssemblyAddress(state.session, g_selectedFrame) != 0;
    ImGui::Text("Assembly positions for frame %d", g_selectedFrame + 1);
    ImGui::SameLine();
    ImGui::TextDisabled(independent ? "Independent layout" : "Original shared layout");
    if (!independent) {
        ImGui::BeginDisabled(!state.session.IsExpandedRom());
        if (ImGui::Button("Make Frame Independent")) {
            independent = MakeFrameIndependent(state, g_selectedFrame);
        }
        ImGui::EndDisabled();
        if (!state.session.IsExpandedRom() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Expand the ROM first.");
        }
    } else {
        if (ImGui::Button("Use Original Shared Layout")) {
            RestoreFrameSharedLayout(state, g_selectedFrame);
            independent = false;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Only independent positions affect this frame alone.");
    if (!g_layoutStatus.empty()) {
        ImGui::TextDisabled("%s", g_layoutStatus.c_str());
    }

    const unsigned assembly = FrameAssemblyAddress(state.session, g_selectedFrame);
    const auto pieceAddresses = ReadFramePieceAddresses(state.session, g_selectedFrame);

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_ScrollX;
    ImGui::BeginDisabled(!independent);
    if (!ImGui::BeginTable("SelectedFramePositions", kSlotsPerFrame, flags, ImVec2(0.0f, 72.0f))) {
        ImGui::EndDisabled();
        return;
    }
    for (int slot = 0; slot < kSlotsPerFrame; ++slot) {
        char heading[16] = {};
        std::snprintf(heading, sizeof(heading), "Tile %d", slot + 1);
        ImGui::TableSetupColumn(heading, ImGuiTableColumnFlags_WidthFixed, 112.0f);
    }
    ImGui::TableHeadersRow();
    ImGui::TableNextRow();

    static RomUndoSnapshot editSnapshot;
    static bool hasEditSnapshot = false;
    for (int slot = 0; slot < kSlotsPerFrame; ++slot) {
        ImGui::TableSetColumnIndex(slot);
        const unsigned pieceAddress = pieceAddresses[slot];
        if (pieceAddress == 0) {
            ImGui::TextDisabled("Not used");
            continue;
        }

        ImGui::PushID(slot);
        for (int coordinate = 0; coordinate < 2; ++coordinate) {
            if (coordinate != 0) ImGui::SameLine();
            const unsigned coordinateAddress = pieceAddress + static_cast<unsigned>(coordinate);
            int value = static_cast<int8_t>(state.session.ReadRom(coordinateAddress, 1));
            ImGui::TextUnformatted(coordinate == 0 ? "X" : "Y");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(34.0f);
            ImGui::PushID(coordinate);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.31f, 0.19f, 0.07f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.46f, 0.29f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.62f, 0.39f, 0.13f, 1.0f));
            const bool changed = ImGui::InputInt("##position", &value, 0, 0);
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemActivated()) {
                g_selectedSlot = slot;
                g_scrollSheetToSelection = true;
                editSnapshot = state.session.CreateUndoSnapshot(state.selectedEventIndex);
                hasEditSnapshot = true;
            }
            if (changed) {
                value = std::clamp(value, -128, 127);
                state.session.WriteRom(coordinateAddress, 1,
                    static_cast<unsigned>(static_cast<uint8_t>(value)));
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && hasEditSnapshot) {
                CommitUndoSnapshot(state, std::move(editSnapshot));
                hasEditSnapshot = false;
            } else if (ImGui::IsItemDeactivated() && hasEditSnapshot) {
                hasEditSnapshot = false;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s position for tile slot %d\nAssembly $%06X",
                    coordinate == 0 ? "X" : "Y", slot + 1, assembly);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::EndDisabled();
}

void DrawCharacterSheet(const SC4Core& core, int selectedTile)
{
    ImGui::TextUnformatted("Simon sprite sheet");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Zoom##sheet", &g_sheetZoom, 1, 3, "%dx");
    const float scale = static_cast<float>(g_sheetZoom);
    const float tileSize = 16.0f * scale;
    const ImVec2 size(kSheetColumns * tileSize, kSheetRows * tileSize);
    if (g_scrollSheetToSelection && selectedTile >= 0 && selectedTile < kCharacterTileCount) {
        const float sheetTop = ImGui::GetCursorPosY();
        const float selectedCenter = sheetTop + (selectedTile / kSheetColumns + 0.5f) * tileSize;
        ImGui::SetScrollY((std::max)(0.0f, selectedCenter - ImGui::GetWindowHeight() * 0.5f));
        g_scrollSheetToSelection = false;
    }
    const ImVec2 sheetMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##character-sheet", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(sheetMin, ImVec2(sheetMin.x + size.x, sheetMin.y + size.y), IM_COL32(9, 9, 12, 255));
    for (int tile = 0; tile < kCharacterTileCount; ++tile) {
        const int column = tile % kSheetColumns;
        const int row = tile / kSheetColumns;
        const ImVec2 position(sheetMin.x + column * tileSize, sheetMin.y + row * tileSize);
        DrawCharacterTile(core, drawList, position, tile, scale, g_characterPalette);
    }
    if (selectedTile >= 0 && selectedTile < kCharacterTileCount) {
        const int column = selectedTile % kSheetColumns;
        const int row = selectedTile / kSheetColumns;
        const ImVec2 selectionMin(sheetMin.x + column * tileSize, sheetMin.y + row * tileSize);
        drawList->AddRect(selectionMin, ImVec2(selectionMin.x + tileSize, selectionMin.y + tileSize),
            IM_COL32(255, 220, 45, 255), 0.0f, 0, 2.0f);
    }
}

} // namespace

void DrawPlayerCharacterEditor(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextDisabled("Open a ROM to edit the player character.");
        return;
    }

    const SC4Core& core = state.session.Core();
    if (core.type != 0 || core.region != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
            "The player-character frame table is currently mapped only for the USA Super Castlevania IV ROM.");
        return;
    }

    static ImGuiTextFilter filter;
    filter.Draw("Search", 260.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::SliderInt("Palette", &g_characterPalette, 0, 7);
    ImGui::SameLine();
    ImGui::TextDisabled("Tile IDs are decimal. Make a frame independent before changing only its X/Y positions.");

    const auto selectedTiles = ReadFrameTiles(state.session, g_selectedFrame);
    const int selectedTile = selectedTiles[g_selectedSlot];
    const float sheetPaneWidth = (std::min)(420.0f,
        (std::max)(170.0f, kSheetColumns * 16.0f * g_sheetZoom + 36.0f));
    ImGui::BeginChild("PlayerCharacterSheetPane", ImVec2(sheetPaneWidth, 0.0f), ImGuiChildFlags_Borders);
    DrawCharacterSheet(core, selectedTile);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("PlayerCharacterDataPane", ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("PlayerCharacterPreview", ImVec2(0.0f, 190.0f), ImGuiChildFlags_Borders);
    DrawCompiledFrame(state);
    ImGui::EndChild();
    ImGui::BeginChild("PlayerCharacterPositions", ImVec2(0.0f, 144.0f), ImGuiChildFlags_Borders);
    DrawSelectedFramePositions(state);
    ImGui::EndChild();

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable;
    constexpr int tableColumnCount = 2 + kSlotsPerFrame;
    if (!ImGui::BeginTable("PlayerCharacterFrames", tableColumnCount, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::EndChild();
        return;
    }

    ImGui::TableSetupScrollFreeze(2, 1);
    ImGui::TableSetupColumn("Frame ID", ImGuiTableColumnFlags_WidthFixed, 62.0f);
    ImGui::TableSetupColumn("Animation", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    for (int slot = 0; slot < kSlotsPerFrame; ++slot) {
        char tileHeading[16] = {};
        std::snprintf(tileHeading, sizeof(tileHeading), "Tile %d", slot + 1);
        ImGui::TableSetupColumn(tileHeading, ImGuiTableColumnFlags_WidthFixed, 58.0f);
    }
    ImGui::TableHeadersRow();

    static RomUndoSnapshot editSnapshot;
    static bool hasEditSnapshot = false;
    for (int frame = 0; frame < kFrameCount; ++frame) {
        if (!filter.PassFilter(kFrameNames[frame])) {
            continue;
        }
            ImGui::PushID(frame);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", frame + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kFrameNames[frame]);

            for (int slot = 0; slot < kSlotsPerFrame; ++slot) {
                ImGui::TableNextColumn();
                const unsigned address = kSimonFrameTable + static_cast<unsigned>(frame * kSlotsPerFrame + slot) * 2u;
                const unsigned rawOffset = state.session.ReadRom(address, 2);
                int tile = DecodeCharacterTile(rawOffset);
                const bool valid = tile >= 0;
                if (!valid) tile = 0;

                ImGui::PushID(slot);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.19f, 0.31f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.29f, 0.47f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.17f, 0.38f, 0.61f, 1.0f));
                const bool changed = ImGui::InputInt("##tile", &tile, 0, 0);
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemActivated()) {
                    g_selectedFrame = frame;
                    g_selectedSlot = slot;
                    g_scrollSheetToSelection = true;
                    editSnapshot = state.session.CreateUndoSnapshot(state.selectedEventIndex);
                    hasEditSnapshot = true;
                }
                if (changed) {
                    state.session.WriteRom(address, 2, EncodeCharacterTile(tile));
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && hasEditSnapshot) {
                    CommitUndoSnapshot(state, std::move(editSnapshot));
                    hasEditSnapshot = false;
                } else if (ImGui::IsItemDeactivated() && hasEditSnapshot) {
                    hasEditSnapshot = false;
                }
                if (ImGui::IsItemHovered()) {
                    if (valid) ImGui::SetTooltip("Frame %d, slot %d\nROM value: %04X", frame + 1, slot + 1, rawOffset);
                    else ImGui::SetTooltip("Unexpected ROM value: %04X", rawOffset);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::EndChild();
}
