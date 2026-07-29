#pragma once

#include "imgui.h"

#include <array>
#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct HWND__;
typedef HWND__* HWND;
struct EditorState;
struct EventInfo;
class RomSession;
class SC4Core;

class LevelRenderer {
public:
    ~LevelRenderer();

    void Invalidate();
    bool EnsureTexture(ID3D11Device* device, RomSession& session);
    void Draw(ImVec2 available, EditorState& state);
    bool DrawBlockPalette(RomSession& session, uint16_t* selectedBlock);
    void DrawBlockEditor(EditorState& state);
    void DrawTileBehaviorEditor(EditorState& state);
    bool CopyAvailableTilesToClipboard(HWND hwnd, RomSession& session, unsigned palette) const;
    bool PasteClipboardToAvailableTiles(HWND hwnd, RomSession& session, unsigned palette);
    void DrawEventThumbnail(SC4Core& core, ImDrawList* drawList, ImVec2 min, ImVec2 max, const EventInfo& event);

private:
    void ReleaseTexture();
    void RenderLevelToPixels(SC4Core& core);
    void RenderBlock(SC4Core& core, int blockX, int blockY, uint16_t block);
    void RenderTile(SC4Core& core, int dstX, int dstY, uint16_t tile);
    void HandleLevelInteractions(EditorState& state, ImVec2 imageMin, float zoom);
    void DrawGridOverlay(ImDrawList* drawList, ImVec2 imageMin, float zoom) const;
    void DrawCameraBoxOverlay(EditorState& state, ImDrawList* drawList, ImVec2 imageMin, float zoom) const;
    void DrawCollisionOverlay(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom);
    void DrawEventOverlay(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, int* selectedEventIndex);
    void DrawPaintPreview(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, EditorState& state, bool hovered);
    void DrawEventSprite(SC4Core& core, const EventInfo& event, ImDrawList* drawList, ImVec2 imageMin, float zoom);
    void DrawSpriteAssembly(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, int originX, int originY, unsigned assemblyOffset, unsigned slotOffset);
    void DrawSpriteTile(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, int dstX, int dstY, unsigned tile, unsigned palette, bool flipX, bool flipY);
    void DrawBlockPreview(SC4Core& core, ImDrawList* drawList, ImVec2 pos, uint16_t block, float scale, ImU8 alpha = 255) const;
    void DrawTilePreview(SC4Core& core, ImDrawList* drawList, ImVec2 pos, uint16_t tile, float scale, ImU8 alpha = 255, int paletteOverride = -1) const;
    unsigned GetBlockOffset(const SC4Core& core, uint16_t block) const;
    bool GetBlockAtLevelPoint(SC4Core& core, int levelX, int levelY, uint16_t& block) const;
    bool SetBlockAtLevelPoint(SC4Core& core, int levelX, int levelY, uint16_t block);
    int HitTestEvent(SC4Core& core, int levelX, int levelY) const;
    EventInfo* EventByIndex(SC4Core& core, int eventIndex) const;
    ImU32 CollisionColor(uint16_t collisionType) const;
    uint32_t ConvertColor(uint16_t color) const;
    void PutPixel(int x, int y, uint32_t color);

    ID3D11ShaderResourceView* textureView_ = nullptr;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
    bool dirty_ = true;
    bool middlePanActive_ = false;
    int draggingEventIndex_ = -1;
    int dragOffsetX_ = 0;
    int dragOffsetY_ = 0;
    bool draggingEventCopy_ = false;
    bool selectingBlockBrush_ = false;
    int blockBrushStartX_ = 0;
    int blockBrushStartY_ = 0;
    int blockBrushCurrentX_ = 0;
    int blockBrushCurrentY_ = 0;
    std::array<uint16_t, 16> blockClipboard_ = {};
    uint16_t blockClipboardSource_ = 0;
    bool hasBlockClipboard_ = false;
    std::vector<uint32_t> pixels_;
};
