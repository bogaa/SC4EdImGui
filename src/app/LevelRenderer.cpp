#include "LevelRenderer.h"

#include <d3d11.h>

#include "EventNames.h"
#include "EventDragDrop.h"
#include "EditorState.h"
#include "EditorUndo.h"
#include "RomSession.h"
#include "SC4Core.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <utility>

namespace {

static bool CanReadRom(const SC4Core& core, unsigned pcOffset, unsigned bytes)
{
    return core.rom && pcOffset <= core.romSize && bytes <= core.romSize - pcOffset;
}

static unsigned ReadByteAt(const SC4Core& core, unsigned snesAddress)
{
    const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(snesAddress));
    return CanReadRom(core, pcOffset, 1) ? core.rom[pcOffset] : 0;
}

static unsigned ReadWordAt(const SC4Core& core, unsigned snesAddress)
{
    const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(snesAddress));
    return CanReadRom(core, pcOffset, 2) ? *reinterpret_cast<const WORD*>(core.rom + pcOffset) : 0;
}

static float ClampSafe(float value, float low, float high)
{
    if (high < low) {
        return low;
    }
    return std::clamp(value, low, high);
}

static unsigned EventAssemblyOffset(const EventInfo& event)
{
    switch (event.eventId & 0xFF) {
    case 0x01: return 0x9061;
    case 0x02: return 0x900C;
    case 0x03: return 0xA4F8;
    case 0x04: return 0xA693;
    case 0x07: return 0x916B;
    case 0x08: return 0x89D1;
    case 0x09: return 0x8BEA;
    case 0x0A: return 0x8CFA;
    case 0x0B: return 0x8D70;
    case 0x0C: return 0x8D61;
    case 0x0D: return 0x8F2E;
    case 0x0E: return 0x80D2;
    case 0x0F: return 0x8EAB;
    case 0x10: return 0x8EB9;
    case 0x11:
    case 0x12: return 0x8AF4;
    case 0x16: return 0xA682;
    case 0x17: return 0xA6EF;
    case 0x2A: return 0xAD7D;
    case 0x2C: return 0x99EE;
    case 0x30: return 0x8DEF;
    case 0x31: return 0x93BE;
    case 0x32: return 0x8A41;
    case 0x33: return 0x8B66;
    case 0x34: return 0x8D89;
    case 0x35: return 0x9A8C;
    case 0x36: return 0x9AC7;
    case 0x37: return 0xA5A5;
    case 0x3A: return 0xA597;
    case 0x3B: return 0xA54A;
    case 0x3C: return 0x8AA3;
    case 0x3E: return 0x8976;
    case 0x3F: return 0xA5AA;
    case 0x40: return 0xA54A;
    case 0x42: return 0x90D7;
    case 0x43: return 0x97BB;
    case 0x44: return 0xA5D1;
    case 0x4A: return 0xA754;
    case 0x4B: return 0x8A6F;
    case 0x4C: return 0x8C58;
    case 0x4D: return 0xA7BC;
    case 0x4E: return 0x918E;
    case 0x51: return 0x8C6D;
    case 0x52: return 0x8F5C;
    case 0x53: return 0x814F;
    case 0x54: return 0x9226;
    case 0x56: return 0x919D;
    case 0x57: return 0x9387;
    case 0x58: return 0x92FB;
    case 0x59: return 0x9390;
    case 0x5A: return 0x8E41;
    case 0x5B: return 0x8E6B;
    case 0x5C: return 0x9A6A;
    case 0x5D: return 0x95A1;
    case 0x5E: return 0x94C7;
    case 0x60: return 0xA50C;
    case 0x61: return 0xA85B;
    case 0x62: return 0xA84A;
    case 0x64: return 0xA869;
    case 0x66: return 0xE07C;
    case 0x69: return 0x9470;
    case 0x6B: return 0x8D50;
    case 0x6C: return 0xA87F;
    case 0x6D: return 0x965A;
    case 0x6E: return 0x96BE;
    case 0x6F: return 0x9648;
    case 0x71: return 0x962E;
    case 0x70: return 0x968B;
    case 0x72: return 0x9717;
    case 0x73: return 0x96E4;
    case 0x74: return 0x9730;
    case 0x75: return 0x973F;
    case 0x76: return 0x984C;
    case 0x78: return 0x8DCC;
    case 0x79: return 0x8DDD;
    case 0x7B: return 0x8B66;
    case 0x7E: return 0x95EB;
    case 0x7F: return 0x9B59;
    default: break;
    }

    if (event.eventId == 0x06) {
        switch (event.eventSubId & 0xFF) {
        case 0x00: return 0xA74B;
        case 0x01: return 0xA7F0;
        case 0x02: return 0xA7F9;
        case 0x03: return 0xA5A5;
        default:  break;
        }
    }
    
    if (event.eventId == 0x2E) {        // render bats and moon 
        switch (event.eventSubId & 0xFF) {
        case 0x00: return 0xA507;
        case 0x01:
        case 0x02: return 0xE1E4;
        default:  break;
        }
    }
    
    if (event.eventId == 0x38) {        // autospawner sprites
        switch (event.eventSubId & 0xFF) {
        case 0x00: return 0xA507;
        case 0x01: return 0x916B;
        case 0x02: return 0x916B;
        case 0x03: return 0x968B;
        case 0x04: return 0x8C6D;
        case 0x05: return 0x8C6D;
        case 0x06: return 0x916B;
        case 0x07: return 0x92FB;
        case 0x08: return 0xE07C;
        case 0x09: return 0x8D61;
        case 0x0a: return 0xE07C;
        case 0x0b: return 0x968B;
        default:  break;
        }
    }
    
    if (event.eventId == 0x68) {        // failling skelly rocks 
        const unsigned subId = event.eventSubId & 0xFF;
        if (subId >= 0x40 && subId <= 0x43) {
            return 0xA89A;
        }
        if (subId >= 0xC0 && subId <= 0xC6) {
            return 0xA884;
        }
    }
    if (event.eventId == 0x7C) {        // spike gear
        const unsigned subId = event.eventSubId & 0xFF;
        if (subId >= 0x80 && subId <= 0x82) {
            return 0xE47C;
        }
    }
    return 0;
}

static bool TryGetSpriteSlotOffset(const SC4Core& core, const EventInfo& event, unsigned& slotOffset)
{
    slotOffset = 0;
    if (event.eventId == 0 || event.type == EVENT_TYPE_CANDLE) {
        return true;
    }

    const unsigned gfxOffset = ReadWordAt(core, 0x81A900 + 3 * (event.eventId & 0xFF));
    const unsigned levelOffset = ReadWordAt(core, 0x868BCD + 2 * core.level);
    const unsigned spriteLoadPc = SNESCore::snes2pc(static_cast<int>(0x860000 + levelOffset));
    if (!gfxOffset || !CanReadRom(core, spriteLoadPc, 1)) {
        return false;
    }

    const BYTE* spriteLoad = core.rom + spriteLoadPc;
    const unsigned count = *spriteLoad++;
    unsigned slotNum = 0;
    for (unsigned i = 0; i < count && CanReadRom(core, static_cast<unsigned>(spriteLoad - core.rom), 1); ++i) {
        const unsigned index = *spriteLoad++;
        const unsigned spriteCount = ReadByteAt(core, 0x81AA80 + index);
        const unsigned currentGfxOffset = ReadWordAt(core, 0x81A900 + 3 * index);
        if (gfxOffset == currentGfxOffset) {
            slotOffset = ReadWordAt(core, 0x819534 + 0x13A0 + 2 * slotNum);
            return true;
        }
        slotNum += spriteCount;
    }
    return false;
}

static unsigned SpriteSlotOffset(const SC4Core& core, const EventInfo& event)
{
    unsigned slotOffset = 0;
    TryGetSpriteSlotOffset(core, event, slotOffset);
    return slotOffset;
}

static ImU32 EventPlaceholderColor(const EventInfo& event)
{
    switch (event.type) {
    case EVENT_TYPE_ENEMY: return IM_COL32(0, 0, 255, 235);
    case EVENT_TYPE_CANDLE: return IM_COL32(0, 255, 0, 235);
    case EVENT_TYPE_OBJECT: return IM_COL32(255, 8, 127, 235);
    default: return IM_COL32(0, 255, 255, 235);
    }
}

static void DrawEventPlaceholder(ImDrawList* drawList, ImVec2 min, ImVec2 max, const EventInfo& event)
{
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    drawList->AddRect(
        ImVec2(center.x - 5.0f, center.y - 5.0f),
        ImVec2(center.x + 5.0f, center.y + 5.0f),
        EventPlaceholderColor(event),
        0.0f,
        0,
        2.0f);
    drawList->AddRect(min, max, IM_COL32(92, 92, 102, 255));
}

static void WriteExpandedRamToRom(SC4Core& core, unsigned ramOffset, unsigned size)
{
    if (!core.expandedROM || !core.expandedOffset.count(core.level)) {
        return;
    }

    const unsigned ramAddress = 0x7E0000 + ramOffset;
    for (const auto& entry : core.expandedOffset[core.level]) {
        const unsigned baseRamAddress = entry.first;
        const unsigned baseRomOffset = entry.second.first;
        const unsigned regionSize = entry.second.second;
        if (ramAddress >= baseRamAddress && ramAddress + size <= baseRamAddress + regionSize) {
            std::memcpy(core.rom + baseRomOffset + (ramAddress - baseRamAddress), core.ram + ramOffset, size);
            return;
        }
    }
}

static DWORD ColorToDibPixel(uint16_t color)
{
    const DWORD r = ((color >> 10) & 0x1F) * 255 / 31;
    const DWORD g = ((color >> 5) & 0x1F) * 255 / 31;
    const DWORD b = (color & 0x1F) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

static HGLOBAL CreateClipboardDib(const std::vector<DWORD>& pixels, int width, int height)
{
    if (pixels.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }

    const SIZE_T headerSize = sizeof(BITMAPINFOHEADER);
    const SIZE_T pixelsSize = static_cast<SIZE_T>(width) * static_cast<SIZE_T>(height) * sizeof(DWORD);
    HGLOBAL dibHandle = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelsSize);
    if (!dibHandle) {
        return nullptr;
    }

    LPBYTE dib = static_cast<LPBYTE>(GlobalLock(dibHandle));
    if (!dib) {
        GlobalFree(dibHandle);
        return nullptr;
    }

    BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(dib);
    ZeroMemory(header, sizeof(BITMAPINFOHEADER));
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = width;
    header->biHeight = height;
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(pixelsSize);

    DWORD* dst = reinterpret_cast<DWORD*>(dib + headerSize);
    for (int y = 0; y < height; ++y) {
        std::memcpy(dst + (height - 1 - y) * width, pixels.data() + y * width, width * sizeof(DWORD));
    }

    GlobalUnlock(dibHandle);
    return dibHandle;
}

static bool CopyPixelsToClipboard(HWND hwnd, const std::vector<DWORD>& pixels, int width, int height)
{
    if (pixels.empty() || width <= 0 || height <= 0) {
        return false;
    }

    HGLOBAL dibHandle = CreateClipboardDib(pixels, width, height);
    if (!dibHandle) {
        return false;
    }

    if (!OpenClipboard(hwnd)) {
        GlobalFree(dibHandle);
        return false;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_DIB, dibHandle)) {
        GlobalFree(dibHandle);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

static HBITMAP CreateBitmapFromClipboardDib(HGLOBAL dibHandle)
{
    if (!dibHandle) {
        return nullptr;
    }

    LPBYTE dib = static_cast<LPBYTE>(GlobalLock(dibHandle));
    if (!dib) {
        return nullptr;
    }

    BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(dib);
    if (header->biSize < sizeof(BITMAPINFOHEADER)) {
        GlobalUnlock(dibHandle);
        return nullptr;
    }

    DWORD colorCount = header->biClrUsed;
    if (!colorCount && header->biBitCount <= 8) {
        colorCount = 1u << header->biBitCount;
    }

    DWORD masksSize = 0;
    if (header->biCompression == BI_BITFIELDS && (header->biBitCount == 16 || header->biBitCount == 32)) {
        masksSize = sizeof(DWORD) * 3;
    }

    LPBYTE bits = dib + header->biSize + masksSize + colorCount * sizeof(RGBQUAD);
    HDC hdc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBitmap(hdc, header, CBM_INIT, bits, reinterpret_cast<BITMAPINFO*>(header), DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    GlobalUnlock(dibHandle);
    return bitmap;
}

static HBITMAP GetClipboardBitmap(bool& owned)
{
    owned = false;
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        return static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    }
    if (IsClipboardFormatAvailable(CF_DIB)) {
        owned = true;
        return CreateBitmapFromClipboardDib(static_cast<HGLOBAL>(GetClipboardData(CF_DIB)));
    }
    return nullptr;
}

static bool ReadClipboardPixels(HWND hwnd, std::vector<COLORREF>& pixels, int& width, int& height)
{
    pixels.clear();
    width = 0;
    height = 0;

    if (!OpenClipboard(hwnd)) {
        return false;
    }

    bool ownedBitmap = false;
    HBITMAP bitmap = GetClipboardBitmap(ownedBitmap);
    if (!bitmap) {
        CloseClipboard();
        return false;
    }

    BITMAP bm = {};
    if (!GetObject(bitmap, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        if (ownedBitmap) {
            DeleteObject(bitmap);
        }
        CloseClipboard();
        return false;
    }

    width = bm.bmWidth;
    height = bm.bmHeight;
    std::vector<DWORD> rawPixels(static_cast<size_t>(width) * static_cast<size_t>(height));

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    const bool ok = GetDIBits(hdc, bitmap, 0, height, rawPixels.data(), &bmi, DIB_RGB_COLORS) != 0;
    ReleaseDC(nullptr, hdc);
    if (ownedBitmap) {
        DeleteObject(bitmap);
    }
    CloseClipboard();

    if (!ok) {
        return false;
    }

    pixels.resize(rawPixels.size());
    for (size_t i = 0; i < rawPixels.size(); ++i) {
        const DWORD bgra = rawPixels[i];
        pixels[i] = RGB((bgra >> 16) & 0xFF, (bgra >> 8) & 0xFF, bgra & 0xFF);
    }
    return true;
}

}

LevelRenderer::~LevelRenderer()
{
    ReleaseTexture();
}

void LevelRenderer::Invalidate()
{
    dirty_ = true;
}

bool LevelRenderer::EnsureTexture(ID3D11Device* device, RomSession& session)
{
    if (!session.IsLoaded()) {
        return false;
    }

    SC4Core& core = session.Core();
    const int width = static_cast<int>(core.levelWidth) * 256;
    const int height = static_cast<int>(core.levelHeight) * 256;
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (!dirty_ && textureView_ && textureWidth_ == width && textureHeight_ == height) {
        return true;
    }

    ReleaseTexture();
    textureWidth_ = width;
    textureHeight_ = height;
    RenderLevelToPixels(core);

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(textureWidth_);
    textureDesc.Height = static_cast<UINT>(textureHeight_);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData = {};
    initialData.pSysMem = pixels_.data();
    initialData.SysMemPitch = static_cast<UINT>(textureWidth_ * sizeof(uint32_t));

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&textureDesc, &initialData, &texture))) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = textureDesc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = 1;
    const HRESULT result = device->CreateShaderResourceView(texture, &viewDesc, &textureView_);
    texture->Release();
    dirty_ = FAILED(result);
    return SUCCEEDED(result);
}

void LevelRenderer::Draw(ImVec2 available, EditorState& state)
{
    if (!textureView_) {
        return;
    }
    RomSession& session = state.session;
    float zoom = state.zoom;

    ImGui::BeginChild("level-scroll", available, false, ImGuiWindowFlags_HorizontalScrollbar);
    if (state.internalEmulatorRunning && state.followInternalEmulatorCamera && state.hasInternalEmulatorCamera) {
      //  const float targetX = static_cast<float>(state.internalEmulatorCameraX) * zoom;
      //  const float targetY = static_cast<float>(state.internalEmulatorCameraY) * zoom;
      //  ImGui::SetScrollX(std::clamp(targetX, 0.0f, ImGui::GetScrollMaxX()));
      //  ImGui::SetScrollY(std::clamp(targetY, 0.0f, ImGui::GetScrollMaxY()));
    }
    const bool levelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (levelHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float wheel = ImGui::GetIO().MouseWheel;
        state.zoom = std::clamp(state.zoom + wheel * 0.1f, 1.0f, 4.0f);
        zoom = state.zoom;
        ImGui::SetScrollY(ImGui::GetScrollY());
    }
    if (levelHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        middlePanActive_ = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        middlePanActive_ = false;
    }
    if (middlePanActive_) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (delta.x != 0.0f) {
            ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
        }
        if (delta.y != 0.0f) {
            ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    const ImVec2 imageSize(static_cast<float>(textureWidth_) * zoom, static_cast<float>(textureHeight_) * zoom);
    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<ImTextureID>(textureView_), imageSize);
    const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    HandleLevelInteractions(state, imageMin, zoom);
    drawList->PushClipRect(imageMin, imageMax, true);
    if (state.showGrid) {
        DrawGridOverlay(drawList, imageMin, zoom);
    }
    DrawCameraBoxOverlay(state, drawList, imageMin, zoom);
    if (state.showCollision && !state.showBackground) {
        DrawCollisionOverlay(session.Core(), drawList, imageMin, zoom);
    }
    if (state.editLevelMode) {
        DrawPaintPreview(session.Core(), drawList, imageMin, zoom, state, levelHovered);
        if (selectingBlockBrush_) {
            const int minX = (std::min)(blockBrushStartX_, blockBrushCurrentX_);
            const int maxX = (std::max)(blockBrushStartX_, blockBrushCurrentX_);
            const int minY = (std::min)(blockBrushStartY_, blockBrushCurrentY_);
            const int maxY = (std::max)(blockBrushStartY_, blockBrushCurrentY_);
            const ImVec2 selectMin(
                imageMin.x + static_cast<float>(minX * 32) * zoom,
                imageMin.y + static_cast<float>(minY * 32) * zoom);
            const ImVec2 selectMax(
                imageMin.x + static_cast<float>((maxX + 1) * 32) * zoom,
                imageMin.y + static_cast<float>((maxY + 1) * 32) * zoom);
            drawList->AddRectFilled(selectMin, selectMax, IM_COL32(255, 230, 70, 28));
            drawList->AddRect(selectMin, selectMax, IM_COL32(255, 230, 70, 240), 0.0f, 0, 2.0f);
        }
    }
    if (state.showEvents && !state.showBackground) {
        DrawEventOverlay(session.Core(), drawList, imageMin, zoom, &state.selectedEventIndex);
    }
    drawList->PopClipRect();
    ImGui::EndChild();
}

void LevelRenderer::DrawGridOverlay(ImDrawList* drawList, ImVec2 imageMin, float zoom) const
{
    const float step = 16.0f * zoom;
    if (step < 2.0f) {
        return;
    }

    const ImU32 minorColor = IM_COL32(255, 255, 255, 42);
    const ImU32 majorColor = IM_COL32(255, 230, 80, 72);
    const float width = static_cast<float>(textureWidth_) * zoom;
    const float height = static_cast<float>(textureHeight_) * zoom;
    const int columns = textureWidth_ / 16;
    const int rows = textureHeight_ / 16;
    for (int x = 0; x <= columns; ++x) {
        const float px = imageMin.x + static_cast<float>(x) * step;
        const bool major = (x % 2) == 0;
        drawList->AddLine(ImVec2(px, imageMin.y), ImVec2(px, imageMin.y + height), major ? majorColor : minorColor, major ? 1.2f : 1.0f);
    }
    for (int y = 0; y <= rows; ++y) {
        const float py = imageMin.y + static_cast<float>(y) * step;
        const bool major = (y % 2) == 0;
        drawList->AddLine(ImVec2(imageMin.x, py), ImVec2(imageMin.x + width, py), major ? majorColor : minorColor, major ? 1.2f : 1.0f);
    }
}

void LevelRenderer::DrawCameraBoxOverlay(EditorState& state, ImDrawList* drawList, ImVec2 imageMin, float zoom) const
{
    if (!state.session.IsLoaded() || !state.session.IsExpandedRom()) {
        return;
    }

    const unsigned checkpoint = static_cast<unsigned>(std::clamp(state.checkpoint, 0, 7));
    const unsigned entranceBase = 0xA78000 + 0x100 * static_cast<unsigned>(state.level) + 0x20 * checkpoint;
    int left = static_cast<int>(state.session.ReadRom(entranceBase + 0x12, 2));
    int right = static_cast<int>(state.session.ReadRom(entranceBase + 0x14, 2)) + 256;
    int top = static_cast<int>(state.session.ReadRom(entranceBase + 0x16, 2));
    int bottom = static_cast<int>(state.session.ReadRom(entranceBase + 0x18, 2)) + 224;

    if (right < left) {
        std::swap(left, right);
    }
    if (bottom < top) {
        std::swap(top, bottom);
    }

    const int levelWidth = textureWidth_;
    const int levelHeight = textureHeight_;
    left = std::clamp(left, 0, levelWidth);
    right = std::clamp(right, 0, levelWidth);
    top = std::clamp(top, 0, levelHeight);
    bottom = std::clamp(bottom, 0, levelHeight);
    if (right <= left || bottom <= top) {
        return;
    }

    const ImVec2 cameraMin(imageMin.x + static_cast<float>(left) * zoom, imageMin.y + static_cast<float>(top) * zoom);
    const ImVec2 cameraMax(imageMin.x + static_cast<float>(right) * zoom, imageMin.y + static_cast<float>(bottom) * zoom);
    drawList->AddRect(cameraMin, cameraMax, IM_COL32(255, 218, 64, 230), 0.0f, 0, 2.0f);
    drawList->AddRect(ImVec2(cameraMin.x + 2.0f, cameraMin.y + 2.0f), ImVec2(cameraMax.x - 2.0f, cameraMax.y - 2.0f), IM_COL32(20, 20, 20, 170), 0.0f, 0, 1.0f);

    char label[64] = {};
    std::snprintf(label, sizeof(label), "Camera %d", checkpoint);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 clipMin = drawList->GetClipRectMin();
    const ImVec2 clipMax = drawList->GetClipRectMax();
    const ImVec2 labelPos(
        ClampSafe(cameraMin.x + 5.0f, clipMin.x + 2.0f, clipMax.x - textSize.x - 2.0f),
        ClampSafe(cameraMin.y + 5.0f, clipMin.y + 2.0f, clipMax.y - textSize.y - 2.0f));
    drawList->AddText(ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, 220), label);
    drawList->AddText(labelPos, IM_COL32(255, 245, 170, 255), label);
}

bool LevelRenderer::DrawBlockPalette(RomSession& session, uint16_t* selectedBlock)
{
    if (!session.IsLoaded() || !selectedBlock) {
        ImGui::TextUnformatted("Open a ROM to browse level blocks.");
        return false;
    }

    SC4Core& core = session.Core();
    const int blockCount = static_cast<int>(core.numBlocks);
    if (blockCount <= 0) {
        ImGui::TextDisabled("No blocks loaded.");
        return false;
    }

    bool selectionChanged = false;

    ImGui::Text("Selected block: %u", (static_cast<unsigned>(*selectedBlock)) & 0x3fffu);
    ImGui::SameLine();
    ImGui::TextDisabled("%d blocks", blockCount);

    if (!core.isMode7()) {
        const unsigned selectedBlockIndex = *selectedBlock & (core.numBlocks - 1);
        const unsigned blockOffset = GetBlockOffset(core, static_cast<uint16_t>(selectedBlockIndex));
        int blockPaletteId = (*reinterpret_cast<WORD*>(core.ram + blockOffset) >> 10) & 0x7;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("Palette ID", &blockPaletteId)) {
            blockPaletteId = std::clamp(blockPaletteId, 0, 7);
            for (unsigned i = 0; i < 16; ++i) {
                WORD* tileMap = reinterpret_cast<WORD*>(core.ram + blockOffset + i * 2);
                *tileMap = static_cast<WORD>((*tileMap & ~0x1C00) | ((blockPaletteId & 0x7) << 10));
            }
            WriteExpandedRamToRom(core, blockOffset, 32);
            Invalidate();
        }
    }

    ImGui::BeginChild("block-palette-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const float previewSize = 48.0f;
    const float labelHeight = ImGui::GetTextLineHeight() + 4.0f;
    const float cellWidth = previewSize + 10.0f;
    const float cellHeight = previewSize + labelHeight + 8.0f;
    const float cellSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float gridWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    const int columns = (std::max)(1, static_cast<int>((gridWidth + cellSpacing) / (cellWidth + cellSpacing)));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int block = 0; block < blockCount; ++block) {
        ImGui::PushID(block);
        if (block > 0 && block % columns != 0) {
            ImGui::SameLine();
        }

        ImVec2 cellMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("block", ImVec2(cellWidth, cellHeight))) {
            *selectedBlock = static_cast<uint16_t>(block);
            selectionChanged = true;
        }

        if (ImGui::IsItemVisible()) {
            const bool selected = (*selectedBlock & (core.numBlocks - 1)) == static_cast<uint16_t>(block);
            const ImVec2 previewMin(cellMin.x + 5.0f, cellMin.y + 3.0f);
            drawList->AddRectFilled(previewMin, ImVec2(previewMin.x + previewSize, previewMin.y + previewSize), IM_COL32(10, 12, 14, 255));
            DrawBlockPreview(core, drawList, previewMin, static_cast<uint16_t>(block), previewSize / 32.0f);
            drawList->AddRect(previewMin, ImVec2(previewMin.x + previewSize, previewMin.y + previewSize), selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(90, 105, 112, 190), 0.0f, 0, selected ? 2.5f : 1.0f);

            char label[8] = {};
            std::snprintf(label, sizeof(label), "%d", block);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(cellMin.x + (cellWidth - textSize.x) * 0.5f, previewMin.y + previewSize + 3.0f), IM_COL32(220, 225, 225, 255), label);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    return selectionChanged;
}

void LevelRenderer::DrawBlockEditor(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Open a ROM to edit blocks.");
        return;
    }

    SC4Core& core = state.session.Core();
    const int blockCount = static_cast<int>(core.numBlocks);
    if (blockCount <= 0) {
        ImGui::TextDisabled("No blocks loaded.");
        return;
    }

    state.selectedBlock = static_cast<uint16_t>(state.selectedBlock & (core.numBlocks - 1));
    state.selectedTile = static_cast<uint16_t>(state.selectedTile & (core.isMode7() ? 0xFF : 0x3FF));
    state.selectedBlockCell = std::clamp(state.selectedBlockCell, 0, 15);
    state.tilePaletteId &= 0x7;

    int blockValue = state.selectedBlock;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Block", &blockValue, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        blockValue = std::clamp(blockValue, 0, blockCount - 1);
        state.selectedBlock = static_cast<uint16_t>(blockValue);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d blocks", blockCount);

    ImGui::TextUnformatted("Blocks");
    ImGui::BeginChild("block-editor-block-list", ImVec2(0.0f, 142.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float blockPreviewSize = 44.0f;
    const float blockLabelHeight = ImGui::GetTextLineHeight() + 4.0f;
    const float blockCellWidth = blockPreviewSize + 10.0f;
    const float blockCellHeight = blockPreviewSize + blockLabelHeight + 8.0f;
    const float blockCellSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float blockGridWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    const int blockColumns = (std::max)(1, static_cast<int>((blockGridWidth + blockCellSpacing) / (blockCellWidth + blockCellSpacing)));
    for (int block = 0; block < blockCount; ++block) {
        ImGui::PushID(block);
        if (block > 0 && block % blockColumns != 0) {
            ImGui::SameLine();
        }

        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("block-thumb", ImVec2(blockCellWidth, blockCellHeight))) {
            state.selectedBlock = static_cast<uint16_t>(block);
        }
        const bool blockHovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopupContextItem("block-actions")) {
            state.selectedBlock = static_cast<uint16_t>(block);
            const auto pasteBlock = [&](bool flipHorizontal, bool flipVertical) {
                const unsigned destinationOffset = GetBlockOffset(core, static_cast<uint16_t>(block));
                WORD* destinationTiles = reinterpret_cast<WORD*>(core.ram + destinationOffset);
                PushUndo(state);
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        const int sourceX = flipHorizontal ? 3 - x : x;
                        const int sourceY = flipVertical ? 3 - y : y;
                        WORD tileMap = blockClipboard_[static_cast<size_t>(sourceY * 4 + sourceX)];
                        if (!core.isMode7()) {
                            if (flipHorizontal) {
                                tileMap ^= 0x4000;
                            }
                            if (flipVertical) {
                                tileMap ^= 0x8000;
                            }
                        }
                        destinationTiles[y * 4 + x] = tileMap;
                    }
                }
                WriteExpandedRamToRom(core, destinationOffset, 32);
                state.selectedBlock = static_cast<uint16_t>(block);
                Invalidate();
            };

            if (ImGui::MenuItem("Copy Block")) {
                const unsigned sourceOffset = GetBlockOffset(core, static_cast<uint16_t>(block));
                const WORD* sourceTiles = reinterpret_cast<const WORD*>(core.ram + sourceOffset);
                std::copy_n(sourceTiles, blockClipboard_.size(), blockClipboard_.begin());
                blockClipboardSource_ = static_cast<uint16_t>(block);
                hasBlockClipboard_ = true;
            }

            if (!hasBlockClipboard_) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem("Paste Replace Block")) {
                pasteBlock(false, false);
            }
            if (ImGui::MenuItem("Paste Horizontal Flipped")) {
                pasteBlock(true, false);
            }
            if (ImGui::MenuItem("Paste Vertical Flipped")) {
                pasteBlock(false, true);
            }
            if (ImGui::MenuItem("Paste Both Flipped")) {
                pasteBlock(true, true);
            }
            if (!hasBlockClipboard_) {
                ImGui::EndDisabled();
            } else {
                ImGui::TextDisabled("Copied from block %u", static_cast<unsigned>(blockClipboardSource_));
            }
            ImGui::EndPopup();
        }
        if (blockHovered) {
            ImGui::SetTooltip("Block %d", block);
        }
        if (ImGui::IsItemVisible()) {
            const bool selected = state.selectedBlock == static_cast<uint16_t>(block);
            const ImVec2 previewMin(cellMin.x + 5.0f, cellMin.y + 3.0f);
            drawList->AddRectFilled(previewMin, ImVec2(previewMin.x + blockPreviewSize, previewMin.y + blockPreviewSize), IM_COL32(10, 12, 14, 255));
            DrawBlockPreview(core, drawList, previewMin, static_cast<uint16_t>(block), blockPreviewSize / 32.0f);
            drawList->AddRect(previewMin, ImVec2(previewMin.x + blockPreviewSize, previewMin.y + blockPreviewSize), selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(90, 105, 112, 190), 0.0f, 0, selected ? 2.5f : 1.0f);

            char label[8] = {};
            std::snprintf(label, sizeof(label), "%d", block);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(cellMin.x + (blockCellWidth - textSize.x) * 0.5f, previewMin.y + blockPreviewSize + 3.0f), IM_COL32(220, 225, 225, 255), label);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    const unsigned blockOffset = GetBlockOffset(core, state.selectedBlock);
    WORD* blockTiles = reinterpret_cast<WORD*>(core.ram + blockOffset);
    static bool blockPaintUndoActive = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        blockPaintUndoActive = false;
    }

    const auto selectedTileMap = [&]() -> WORD {
        return static_cast<WORD>(
            (state.selectedTile & (core.isMode7() ? 0xFF : 0x3FF))
            | (core.isMode7() ? 0 : ((state.tilePaletteId & 0x7) << 10))
            | (!core.isMode7() && state.tileLayerPriority ? 0x2000 : 0)
            | (state.tileFlipX ? 0x4000 : 0)
            | (state.tileFlipY ? 0x8000 : 0));
    };

    const auto copyCellToBrush = [&](int cell) {
        const WORD map = blockTiles[cell];
        state.selectedBlockCell = cell;
        state.selectedTile = static_cast<uint16_t>(core.isMode7() ? (map & 0xFF) : (map & 0x3FF));
        state.tilePaletteId = core.isMode7() ? 0 : ((map >> 10) & 0x7);
        state.tileLayerPriority = !core.isMode7() && (map & 0x2000) != 0;
        state.tileFlipX = (map & 0x4000) != 0;
        state.tileFlipY = (map & 0x8000) != 0;
    };

    const auto paintCell = [&](int cell) {
        const WORD newMap = selectedTileMap();
        if (blockTiles[cell] == newMap) {
            state.selectedBlockCell = cell;
            return;
        }
        if (!blockPaintUndoActive) {
            PushUndo(state);
            blockPaintUndoActive = true;
        }
        blockTiles[cell] = newMap;
        state.selectedBlockCell = cell;
        WriteExpandedRamToRom(core, blockOffset + static_cast<unsigned>(cell) * 2, 2);
        Invalidate();
    };

    ImGui::Columns(2, "block-editor-columns", false);
    drawList = ImGui::GetWindowDrawList();
    ImGui::TextUnformatted("Block");
    const float cellSize = 38.0f;
    for (int cell = 0; cell < 16; ++cell) {
        if (cell > 0 && (cell % 4) != 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(cell);
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("cell", ImVec2(cellSize, cellSize));
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
               paintCell(cell);
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                copyCellToBrush(cell);
            }
        }
        if (ImGui::IsItemVisible()) {
            const bool selected = state.selectedBlockCell == cell;
            drawList->AddRectFilled(cellMin, ImVec2(cellMin.x + cellSize, cellMin.y + cellSize), IM_COL32(9, 11, 13, 255));
            DrawTilePreview(core, drawList, ImVec2(cellMin.x + 3.0f, cellMin.y + 3.0f), blockTiles[cell], 4.0f);
            drawList->AddRect(cellMin, ImVec2(cellMin.x + cellSize, cellMin.y + cellSize), selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(82, 94, 102, 220), 0.0f, 0, selected ? 2.0f : 1.0f);
        }
        ImGui::PopID();
    }

    ImGui::NextColumn();
    ImGui::TextUnformatted("Tile");
    int tileValue = state.selectedTile;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Tile ID", &tileValue, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        tileValue = std::clamp(tileValue, 0, core.isMode7() ? 0xFF : 0x3FF);
        state.selectedTile = static_cast<uint16_t>(tileValue);
    }
    if (!core.isMode7()) {
        int palette = static_cast<int>(state.tilePaletteId & 0x7);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt("Palette ID", &palette)) {
            state.tilePaletteId = static_cast<unsigned>(std::clamp(palette, 0, 7));
        }
        ImGui::Checkbox("H Flip", &state.tileFlipX);
        ImGui::SameLine();
        ImGui::Checkbox("V Flip", &state.tileFlipY);
        ImGui::Checkbox("Layer Priority", &state.tileLayerPriority);
    }

    const WORD previewMap = selectedTileMap();
    const ImVec2 previewMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(66.0f, 66.0f));
    drawList->AddRectFilled(previewMin, ImVec2(previewMin.x + 66.0f, previewMin.y + 66.0f), IM_COL32(9, 11, 13, 255));
    DrawTilePreview(core, drawList, ImVec2(previewMin.x + 1.0f, previewMin.y + 1.0f), previewMap, 8.0f);
    drawList->AddRect(previewMin, ImVec2(previewMin.x + 66.0f, previewMin.y + 66.0f), IM_COL32(92, 112, 125, 255));
    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::TextUnformatted("Available Tiles");
    ImGui::BeginChild("block-editor-tile-list", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    drawList = ImGui::GetWindowDrawList();
    const unsigned tileCount = core.isMode7() ? 0x100u : 0x400u;
    const float tileCellSize = 42.0f;
    const float tileCellSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float tileGridWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    const int columns = (std::max)(1, static_cast<int>((tileGridWidth + tileCellSpacing) / (tileCellSize + tileCellSpacing)));
    for (unsigned tile = 0; tile < tileCount; ++tile) {
        ImGui::PushID(static_cast<int>(tile));
        if (tile > 0 && (tile % static_cast<unsigned>(columns)) != 0) {
            ImGui::SameLine();
        }

        const ImVec2 tileMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("tile", ImVec2(tileCellSize, tileCellSize))) {
            state.selectedTile = static_cast<uint16_t>(tile);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Tile %u", tile);
        }
        if (ImGui::IsItemVisible()) {
            const bool selected = state.selectedTile == tile;
            const WORD map = static_cast<WORD>(
                (tile & (core.isMode7() ? 0xFF : 0x3FF))
                | (core.isMode7() ? 0 : ((state.tilePaletteId & 0x7) << 10))
                | (!core.isMode7() && state.tileLayerPriority ? 0x2000 : 0));
            drawList->AddRectFilled(tileMin, ImVec2(tileMin.x + tileCellSize, tileMin.y + tileCellSize), IM_COL32(10, 12, 14, 255));
            DrawTilePreview(core, drawList, ImVec2(tileMin.x + 5.0f, tileMin.y + 5.0f), map, 4.0f);
            drawList->AddRect(tileMin, ImVec2(tileMin.x + tileCellSize, tileMin.y + tileCellSize), selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(72, 82, 88, 190), 0.0f, 0, selected ? 2.0f : 1.0f);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void LevelRenderer::DrawTileBehaviorEditor(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Open a ROM to edit tile behavior.");
        return;
    }

    SC4Core& core = state.session.Core();
    const unsigned tileCount = core.isMode7() ? 0x100u : 0x400u;
    state.selectedBehaviorTile = static_cast<uint16_t>(state.selectedBehaviorTile % tileCount);
    state.tilePaletteId &= 0x7;

    state.selectedBehaviorTiles.erase(
        std::remove_if(
            state.selectedBehaviorTiles.begin(),
            state.selectedBehaviorTiles.end(),
            [tileCount](uint16_t tile) { return tile >= tileCount; }),
        state.selectedBehaviorTiles.end());
    if (state.selectedBehaviorTiles.empty()) {
        state.selectedBehaviorTiles.push_back(state.selectedBehaviorTile);
    }

    const auto selectOnly = [&](uint16_t tile) {
        state.selectedBehaviorTile = tile;
        state.selectedBehaviorTiles.assign(1, tile);
    };
    const auto applyBehavior = [&](WORD type) {
        if (!core.expandedROM || state.selectedBehaviorTiles.empty()) {
            return;
        }
        PushUndo(state);
        for (uint16_t tile : state.selectedBehaviorTiles) {
            core.SetTileType(tile, type);
        }
        Invalidate();
    };

    if (!core.expandedROM) {
        ImGui::TextDisabled("Per-tile behavior editing needs an expanded ROM.");
        ImGui::TextDisabled("This ROM uses threshold tables, so values are shown read-only.");
        ImGui::Separator();
    }

    ImGui::TextUnformatted("Properties");
    int tileValue = state.selectedBehaviorTile;
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("Tile ID", &tileValue, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        selectOnly(static_cast<uint16_t>(std::clamp(tileValue, 0, static_cast<int>(tileCount) - 1)));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%u selected", static_cast<unsigned>(state.selectedBehaviorTiles.size()));

    if (!core.isMode7()) {
        ImGui::SameLine();
        int palette = static_cast<int>(state.tilePaletteId & 0x7);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt("Palette ID", &palette)) {
            state.tilePaletteId = static_cast<unsigned>(std::clamp(palette, 0, 7));
        }
    }

    const WORD firstBehavior = core.GetTileType(state.selectedBehaviorTiles.front());
    bool mixedBehavior = false;
    for (uint16_t tile : state.selectedBehaviorTiles) {
        if (core.GetTileType(tile) != firstBehavior) {
            mixedBehavior = true;
            break;
        }
    }
    const auto behaviorName = TileTypeMap.find(firstBehavior);
    const char* comboLabel = mixedBehavior
        ? "Mixed"
        : (behaviorName != TileTypeMap.end() ? behaviorName->second.c_str() : "Unknown");

    if (!core.expandedROM) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Behavior", comboLabel)) {
        for (const auto& [type, name] : TileTypeMap) {
            if (type == 0x00) {
                continue;
            }
            char label[64] = {};
            std::snprintf(label, sizeof(label), "%u  %s", type & 0xFF, name.c_str());
            const bool selected = !mixedBehavior && firstBehavior == type;
            if (ImGui::Selectable(label, selected)) {
                applyBehavior(static_cast<WORD>(type));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Platform (FG)")) {
        applyBehavior(0xCA);
    }
    ImGui::SameLine();
    if (ImGui::Button("Step (FG)")) {
        applyBehavior(0xCC);
    }
    ImGui::SameLine();
    if (ImGui::Button("Background")) {
        applyBehavior(0xE4);
    }
    if (!core.expandedROM) {
        ImGui::EndDisabled();
    }

    ImGui::Text("Level table: %s", core.expandedROM ? "expanded per-tile table" : "original threshold table");
    if (core.expandedROM && state.selectedBehaviorTiles.size() == 1) {
        const unsigned behaviorAddress = 0xA28000 + (core.level % 0x20) * 0x400 + (core.level / 0x20) * 0x8000 + state.selectedBehaviorTiles.front();
        ImGui::SameLine();
        ImGui::TextDisabled("ROM address: $%06X", behaviorAddress);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Tiles");
    ImGui::BeginChild("tile-behavior-tile-list", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float tileCellSize = 42.0f;
    const float tileCellSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float tileGridWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    const int columns = (std::max)(1, static_cast<int>((tileGridWidth + tileCellSpacing) / (tileCellSize + tileCellSpacing)));  
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        int delta = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            delta = -1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            delta = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            delta = -columns;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            delta = columns;
        }
        if (delta != 0) {
            const int nextTile = std::clamp(static_cast<int>(state.selectedBehaviorTile) + delta, 0, static_cast<int>(tileCount) - 1);
            selectOnly(static_cast<uint16_t>(nextTile));
        }
    }

    std::vector<bool> selectedTiles(tileCount, false);
    for (uint16_t tile : state.selectedBehaviorTiles) {
        selectedTiles[tile] = true;
    }
    drawList->PushClipRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), true);
    for (unsigned tile = 0; tile < tileCount; ++tile) {
        ImGui::PushID(static_cast<int>(tile));
        if (tile > 0 && (tile % static_cast<unsigned>(columns)) != 0) {
            ImGui::SameLine();
        }

        const ImVec2 tileMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("tile-behavior", ImVec2(tileCellSize, tileCellSize))) {
            const uint16_t clickedTile = static_cast<uint16_t>(tile);
            if (ImGui::GetIO().KeyShift) {
                const unsigned rangeStart = (std::min)(static_cast<unsigned>(state.selectedBehaviorTile), tile);
                const unsigned rangeEnd = (std::max)(static_cast<unsigned>(state.selectedBehaviorTile), tile);
                state.selectedBehaviorTiles.clear();
                for (unsigned selectedTile = rangeStart; selectedTile <= rangeEnd; ++selectedTile) {
                    state.selectedBehaviorTiles.push_back(static_cast<uint16_t>(selectedTile));
                }
                state.selectedBehaviorTile = clickedTile;
            } else if (ImGui::GetIO().KeyCtrl) {
                const auto selectedIt = std::find(state.selectedBehaviorTiles.begin(), state.selectedBehaviorTiles.end(), clickedTile);
                if (selectedIt == state.selectedBehaviorTiles.end()) {
                    state.selectedBehaviorTiles.push_back(clickedTile);
                    state.selectedBehaviorTile = clickedTile;
                } else if (state.selectedBehaviorTiles.size() > 1) {
                    state.selectedBehaviorTiles.erase(selectedIt);
                    state.selectedBehaviorTile = state.selectedBehaviorTiles.back();
                }
            } else {
                selectOnly(clickedTile);
            }
        }
        if (ImGui::IsItemHovered()) {
            const WORD behavior = core.GetTileType(static_cast<WORD>(tile));
            const auto nameIt = TileTypeMap.find(behavior);
            ImGui::SetTooltip("Tile %u\n%s %u", tile, nameIt != TileTypeMap.end() ? nameIt->second.c_str() : "Type", behavior & 0xFF);
        }
        if (ImGui::IsItemVisible()) {
            const bool selected = selectedTiles[tile];
            const WORD map = static_cast<WORD>(
                (tile & (core.isMode7() ? 0xFF : 0x3FF))
                | (core.isMode7() ? 0 : ((state.tilePaletteId & 0xf) << 10)));
            drawList->AddRectFilled(tileMin, ImVec2(tileMin.x + tileCellSize, tileMin.y + tileCellSize), IM_COL32(10, 12, 14, 255));
            DrawTilePreview(core, drawList, ImVec2(tileMin.x + 5.0f, tileMin.y + 5.0f), map, 4.0f);
            drawList->AddRect(tileMin, ImVec2(tileMin.x + tileCellSize, tileMin.y + tileCellSize), selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(72, 82, 88, 190), 0.0f, 0, selected ? 2.0f : 1.0f);
        }
        ImGui::PopID();
    }
    drawList->PopClipRect();
    ImGui::EndChild();
}

bool LevelRenderer::CopyAvailableTilesToClipboard(HWND hwnd, RomSession& session, unsigned palette) const
{
    if (!session.IsLoaded()) {
        return false;
    }

    SC4Core& core = session.Core();
    const unsigned tileCount = core.isMode7() ? 0x100u : 0x400u;
    const int columns = 16;
    const int width = columns * 8;
    const int height = static_cast<int>((tileCount + columns - 1) / columns) * 8;
    if (tileCount == 0 || width <= 0 || height <= 0) {
        return false;
    }

    std::vector<DWORD> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    const unsigned tileBase = core.GetTileVramByteAddr() * 2;
    const unsigned paletteRow = core.isMode7() ? 0 : ((palette & 0x7) << 4);
    for (unsigned tile = 0; tile < tileCount; ++tile) {
        const int tileX = static_cast<int>(tile % columns);
        const int tileY = static_cast<int>(tile / columns);
        const BYTE* raw = core.vramCache + tileBase + (tile << 6);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const BYTE pixel = raw[x + y * 8] & 0x0F;
                pixels[static_cast<size_t>(tileY * 8 + y) * width + tileX * 8 + x] = ColorToDibPixel(core.palCache[pixel | paletteRow]);
            }
        }
    }

    return CopyPixelsToClipboard(hwnd, pixels, width, height);
}

bool LevelRenderer::PasteClipboardToAvailableTiles(HWND hwnd, RomSession& session, unsigned palette)
{
    if (!session.IsLoaded()) {
        return false;
    }

    SC4Core& core = session.Core();
    if (!core.expandedROM || !core.expandedOffset.count(core.level) || !core.expandedOffset[core.level].count(core.GetTileVramByteAddr() >> 1)) {
        return false;
    }

    std::vector<COLORREF> pixels;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    if (!ReadClipboardPixels(hwnd, pixels, bitmapWidth, bitmapHeight)) {
        return false;
    }

    const unsigned tileCount = core.isMode7() ? 0x100u : 0x400u;
    const int columns = 16;
    const int width = (std::min)(bitmapWidth, columns * 8);
    const int height = (std::min)(bitmapHeight, static_cast<int>((tileCount + columns - 1) / columns) * 8);
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::set<unsigned> dirtyTiles;
    const unsigned tileBase = core.GetTileVramByteAddr() * 2;
    const unsigned paletteRow = core.isMode7() ? 0 : ((palette & 0x7) << 4);
    for (int tileY = 0; tileY < height / 8; ++tileY) {
        for (int tileX = 0; tileX < width / 8; ++tileX) {
            const unsigned tile = static_cast<unsigned>(tileY * columns + tileX);
            if (tile >= tileCount) {
                continue;
            }
            BYTE* raw = core.vramCache + tileBase + (tile << 6);
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const COLORREF color = pixels[static_cast<size_t>(tileY * 8 + y) * bitmapWidth + tileX * 8 + x];
                    const int r = GetRValue(color);
                    const int g = GetGValue(color);
                    const int b = GetBValue(color);
                    BYTE bestIndex = 0;
                    int bestDistance = INT_MAX;
                    for (BYTE i = 0; i < 16; ++i) {
                        const uint16_t palColor = core.palCache[i | paletteRow];
                        const int pr = static_cast<int>(((palColor >> 10) & 0x1F) * 255 / 31);
                        const int pg = static_cast<int>(((palColor >> 5) & 0x1F) * 255 / 31);
                        const int pb = static_cast<int>((palColor & 0x1F) * 255 / 31);
                        const int dr = r - pr;
                        const int dg = g - pg;
                        const int db = b - pb;
                        const int distance = dr * dr + dg * dg + db * db;
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            bestIndex = i;
                        }
                    }

                    raw[x + y * 8] = bestIndex;
                }
            }
            dirtyTiles.insert(tile);
        }
    }

    const unsigned baseOffset = core.expandedOffset[core.level][core.GetTileVramByteAddr() >> 1].first;
    for (const unsigned tile : dirtyTiles) {
        BYTE* raw = core.vramCache + tileBase + (tile << 6);
        if (!core.isMode7()) {
            core.raw2tile4bpp(raw, core.rom + baseOffset + (tile << 5));
        } else {
            core.raw2tileMode7(raw, core.rom + baseOffset + (tile << 6));
        }
    }

    Invalidate();
    return true;
}

void LevelRenderer::HandleLevelInteractions(EditorState& state, ImVec2 imageMin, float zoom)
{
    RomSession& session = state.session;
    SC4Core& core = session.Core();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int levelX = static_cast<int>((mouse.x - imageMin.x) / zoom);
    const int levelY = static_cast<int>((mouse.y - imageMin.y) / zoom);

    if (state.editLevelMode) {
        const int blockX = levelX >> 5;
        const int blockY = levelY >> 5;
        uint16_t sampledBlock = 0;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && GetBlockAtLevelPoint(core, levelX, levelY, sampledBlock)) {
            if ((sampledBlock & 0x3FFFu) == (state.selectedBlock & 0x3FFFu)) {
                const uint16_t nextFlags = static_cast<uint16_t>((state.selectedBlock + 0x4000u) & 0xC000u);
                sampledBlock = static_cast<uint16_t>((sampledBlock & 0x3FFFu) | nextFlags);

                // uint16_t mirrorMask = 0x4000;
                // if (core.type == 1) {
                //     mirrorMask = (0x4000u) ^ sampledBlock;
                // }
                // sampledBlock = static_cast<uint16_t>(state.selectedBlock | mirrorMask);
            }

            if (sampledBlock != state.selectedBlock) {
                state.selectedBlock = (sampledBlock);
                state.blockBrushWidth = 1;
                state.blockBrushHeight = 1;
                state.blockBrush.assign(1, sampledBlock);
                selectingBlockBrush_ = true;
                blockBrushStartX_ = blockX;
                blockBrushStartY_ = blockY;
                blockBrushCurrentX_ = blockX;
                blockBrushCurrentY_ = blockY;
            }
        }
        if (selectingBlockBrush_ && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {       // selects a range of blocks for the brush
            const int maxBlockX = (std::max)(0, static_cast<int>(core.levelWidth) * 8 - 1);
            const int maxBlockY = (std::max)(0, static_cast<int>(core.levelHeight) * 8 - 1);
            blockBrushCurrentX_ = std::clamp(blockX, 0, maxBlockX);
            blockBrushCurrentY_ = std::clamp(blockY, 0, maxBlockY);
        }
        if (selectingBlockBrush_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            selectingBlockBrush_ = false;
            const int minX = (std::min)(blockBrushStartX_, blockBrushCurrentX_);
            const int maxX = (std::max)(blockBrushStartX_, blockBrushCurrentX_);
            const int minY = (std::min)(blockBrushStartY_, blockBrushCurrentY_);
            const int maxY = (std::max)(blockBrushStartY_, blockBrushCurrentY_);
            const int width = maxX - minX + 1;
            const int height = maxY - minY + 1;
            if (width > 0 && height > 0) {
                if (width > 1 || height > 1) {
                    std::vector<uint16_t> brush(static_cast<size_t>(width) * static_cast<size_t>(height), state.selectedBlock);
                    for (int y = 0; y < height; ++y) {
                        for (int x = 0; x < width; ++x) {
                            uint16_t block = state.selectedBlock;
                            if (GetBlockAtLevelPoint(core, (minX + x) * 32, (minY + y) * 32, block)) {
                                brush[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = block;
                            }
                        }
                    }
                    state.blockBrush = std::move(brush);
                    state.selectedBlock = state.blockBrush.front();
                }
                state.blockBrushWidth = width;
                state.blockBrushHeight = height;
            }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.levelPaintUndoActive = false;
        }
        if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!state.levelPaintUndoActive) {
                PushUndo(state);
                state.levelPaintUndoActive = true;
            }
            const int width = (std::max)(1, state.blockBrushWidth);
            const int height = (std::max)(1, state.blockBrushHeight);
            const std::vector<uint16_t>& brush = state.blockBrush;
            bool changed = false;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    const uint16_t block = index < brush.size() ? brush[index] : state.selectedBlock;
                    changed |= SetBlockAtLevelPoint(core, (blockX + x) * 32, (blockY + y) * 32, block);
                }
            }
 
            if (changed) {
                Invalidate();
            }
        }
        return;
    }

    if (!state.showEvents || state.showBackground) {
        draggingEventIndex_ = -1;
        draggingEventCopy_ = false;
    } else {
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            const int hitEvent = HitTestEvent(core, levelX, levelY);
            if (hitEvent >= 0) {
                state.selectedEventIndex = hitEvent;
                EventInfo* event = EventByIndex(core, hitEvent);
                if (event) {
                    PushUndo(state);
                    draggingEventIndex_ = hitEvent;
                    draggingEventCopy_ = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                    dragOffsetX_ = levelX - (static_cast<int>(event->xpos) & 0x3FFC);
                    dragOffsetY_ = levelY - (static_cast<int>(event->ypos) & 0x3FFC);

                    if (draggingEventCopy_) {
                        EventInfo copy = *event;
                        if (session.AddEvent(copy, &state.selectedEventIndex)) {
                            draggingEventIndex_ = state.selectedEventIndex;
                        } else {
                            draggingEventIndex_ = -1;
                            draggingEventCopy_ = false;
                        }
                    }
                }
            }
        }

        const bool dragButtonDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (draggingEventIndex_ >= 0 && dragButtonDown) {
            if (EventInfo* event = EventByIndex(core, draggingEventIndex_)) {
                const int maxX = (std::max)(0, textureWidth_ - 1);
                const int maxY = (std::max)(0, textureHeight_ - 1);
                int newX = std::clamp(levelX - dragOffsetX_, 0, maxX);
                int newY = std::clamp(levelY - dragOffsetY_, 0, maxY);
                if (ImGui::GetIO().KeyShift) {
                    newX = std::clamp(((newX + 4) / 8) * 8, 0, maxX);
                    newY = std::clamp(((newY + 4) / 8) * 8, 0, maxY);
                }
                event->xpos = (static_cast<WORD>(newX) & 0x3FFC);
                event->ypos = (static_cast<WORD>(newY) & 0x3FFC);
            }
        }

        if (draggingEventIndex_ >= 0 && !dragButtonDown) {
            session.SaveEvents();
            draggingEventIndex_ = -1;
            draggingEventCopy_ = false;
        }
    }

    if (hovered && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSc4EventDragPayloadType)) {
            if (payload->DataSize == sizeof(EventDragPayload)) {
                PushUndo(state);
                const EventDragPayload drag = *reinterpret_cast<const EventDragPayload*>(payload->Data);
                EventInfo event = {};
                event.type = drag.type;
                event.eventId = drag.eventId;
                event.eventSubId = drag.eventSubId;
                event.xpos = static_cast<WORD>(std::clamp(levelX, 0, (std::max)(0, textureWidth_ - 1)) & 0x3FFC);
                event.ypos = static_cast<WORD>(std::clamp(levelY, 0, (std::max)(0, textureHeight_ - 1)) & 0x3FFC);
                event.match = 0;
                event.spawnIndex = 0;
                event.unknown = drag.unknown;
                if (session.AddEvent(event, &state.selectedEventIndex)) {
                    Invalidate();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void LevelRenderer::ReleaseTexture()
{
    if (textureView_) {
        textureView_->Release();
        textureView_ = nullptr;
    }
}

void LevelRenderer::RenderLevelToPixels(SC4Core& core)
{
    pixels_.assign(static_cast<size_t>(textureWidth_) * static_cast<size_t>(textureHeight_), 0xFF101214u);

    for (int sceneY = 0; sceneY < core.levelHeight; ++sceneY) {
        for (int sceneX = 0; sceneX < core.levelWidth; ++sceneX) {
            const int sceneIndex = sceneY * core.levelWidth + sceneX;
            const WORD* blockMap = core.mapping + (sceneIndex << 6);
            for (int blockY = 0; blockY < 8; ++blockY) {
                for (int blockX = 0; blockX < 8; ++blockX) {
                    uint16_t block = *blockMap++;
                    if (core.type == 1) {
                        block = static_cast<WORD>(((block & 0x8000) >> 1) | ((block & 0x4000) << 1) | ((block & ~0xC000) >> 5));
                    } else if (core.type == 2) {
                        block = static_cast<WORD>(((block & 0x0080) << 7) | (block & ~0xFF00));
                    }
                    RenderBlock(core, sceneX * 8 + blockX, sceneY * 8 + blockY, block);
                }
            }
        }
    }
}

void LevelRenderer::RenderBlock(SC4Core& core, int blockX, int blockY, uint16_t block)
{
    const unsigned blockOffset = GetBlockOffset(core, block);

    uint32_t blockPixels[32 * 32] = {};
    auto oldPixels = pixels_.data();
    std::vector<uint32_t> temp(std::begin(blockPixels), std::end(blockPixels));

    const int savedWidth = textureWidth_;
    const int savedHeight = textureHeight_;
    textureWidth_ = 32;
    textureHeight_ = 32;
    pixels_.swap(temp);

    for (int tileY = 0; tileY < 4; ++tileY) {
        for (int tileX = 0; tileX < 4; ++tileX) {
            const unsigned tileOffset = (tileX << 1) + (tileY << 3);
            const uint16_t tile = *reinterpret_cast<const uint16_t*>(core.ram + blockOffset + tileOffset);
            RenderTile(core, tileX * 8, tileY * 8, tile);
        }
    }

    pixels_.swap(temp);
    textureWidth_ = savedWidth;
    textureHeight_ = savedHeight;
    (void)oldPixels;

    const bool flipX = (block & 0x8000) != 0;
    const bool flipY = (block & 0x4000) != 0;
    const int dstBaseX = blockX * 32;
    const int dstBaseY = blockY * 32;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const int srcX = flipX ? 31 - x : x;
            const int srcY = flipY ? 31 - y : y;
            PutPixel(dstBaseX + x, dstBaseY + y, temp[srcY * 32 + srcX]);
        }
    }
}

void LevelRenderer::DrawCollisionOverlay(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom)
{
    const float tileSize = 8.0f * zoom;
    for (int sceneY = 0; sceneY < core.levelHeight; ++sceneY) {
        for (int sceneX = 0; sceneX < core.levelWidth; ++sceneX) {
            const int sceneIndex = sceneY * core.levelWidth + sceneX;
            const WORD* blockMap = core.mapping + (sceneIndex << 6);
            for (int blockY = 0; blockY < 8; ++blockY) {
                for (int blockX = 0; blockX < 8; ++blockX) {
                    uint16_t block = *blockMap++;
                    if (core.type == 1) {
                        block = static_cast<WORD>(((block & 0x8000) >> 1) | ((block & 0x4000) << 1) | ((block & ~0xC000) >> 5));
                    } else if (core.type == 2) {
                        block = static_cast<WORD>(((block & 0x0080) << 7) | (block & ~0xFF00));
                    }

                    const unsigned blockOffset = GetBlockOffset(core, block);
                    const int baseTileX = sceneX * 32 + blockX * 4;
                    const int baseTileY = sceneY * 32 + blockY * 4;
                    for (int tileY = 0; tileY < 4; ++tileY) {
                        for (int tileX = 0; tileX < 4; ++tileX) {
                            const unsigned tileOffset = (tileX << 1) + (tileY << 3);
                            const uint16_t tile = *reinterpret_cast<const uint16_t*>(core.ram + blockOffset + tileOffset);
                            const uint16_t tileIndex = tile & 0x3FF;
                            const uint16_t collisionType = core.GetTileType(tileIndex);
                            if (collisionType == 0 || collisionType == 0xC8 || collisionType == 0xE4) {
                                continue;
                            }

                            const float x = imageMin.x + static_cast<float>(baseTileX + tileX) * tileSize;
                            const float y = imageMin.y + static_cast<float>(baseTileY + tileY) * tileSize;
                            const ImVec2 a(x, y);
                            const ImVec2 b(x + tileSize, y + tileSize);
                            drawList->AddRectFilled(a, b, CollisionColor(collisionType));
                            if (zoom >= 1.6f) {
                                char text[4] = {};
                                std::snprintf(text, sizeof(text), "%u", collisionType & 0xFF);
                                drawList->AddText(ImVec2(a.x + 1.0f, a.y + 1.0f), IM_COL32(15, 15, 15, 235), text);
                                drawList->AddText(a, IM_COL32(255, 245, 160, 255), text);
                            }
                        }
                    }
                }
            }
        }
    }
}

void LevelRenderer::DrawEventOverlay(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, int* selectedEventIndex)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool canSelect = selectedEventIndex != nullptr && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    int clickedEvent = -1;
    float clickedDistanceSq = 14.0f * 14.0f;
    int eventIndex = 0;
    for (const auto& event : core.eventTable) {
        const float x = imageMin.x + static_cast<float>(event.xpos & 0x3FFC) * zoom;
        const float y = imageMin.y + static_cast<float>(event.ypos & 0x3FFC) * zoom;
        const bool selected = selectedEventIndex != nullptr && *selectedEventIndex == eventIndex;
        DrawEventSprite(core, event, drawList, imageMin, zoom);
        if (canSelect) {
            const float dx = mouse.x - x;
            const float dy = mouse.y - y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq <= clickedDistanceSq) {
                clickedDistanceSq = distanceSq;
                clickedEvent = eventIndex;
            }
        }
        ImU32 color = IM_COL32(95, 150, 255, 235);
        if (event.type == EVENT_TYPE_CANDLE) {
            color = IM_COL32(95, 230, 120, 235);
        } else if (event.type == EVENT_TYPE_OBJECT) {
            color = IM_COL32(255, 105, 170, 235);
        } else if (event.type == EVENT_TYPE_SPECIAL) {
            color = IM_COL32(90, 230, 230, 235);
        }

        if (selected) {
            drawList->AddCircle(ImVec2(x, y), 10.0f, IM_COL32(255, 230, 70, 128), 24, 2.0f);
        }

        if (zoom >= 1.0f) {
            char label[96] = {};
            std::snprintf(label, sizeof(label), "%s",    // "%s  Type %u ID %u.%u",
                EventDisplayName(core, event),
                event.type,
                event.eventId & 0xFF,
                event.eventSubId & 0xFF);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 clipMin = drawList->GetClipRectMin();
            const ImVec2 clipMax = drawList->GetClipRectMax();
            const ImVec2 labelPos(
                ClampSafe(x + 6.0f, clipMin.x + 2.0f, clipMax.x - textSize.x - 2.0f),
                ClampSafe(y - 9.0f, clipMin.y + 2.0f, clipMax.y - textSize.y - 2.0f));
            drawList->AddText(ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, 220), label);
            drawList->AddText(labelPos, IM_COL32(235, 240, 235, 255), label);
        }
        ++eventIndex;
    }
    if (clickedEvent >= 0) {
        *selectedEventIndex = clickedEvent;
    }
}

void LevelRenderer::DrawPaintPreview(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, EditorState& state, bool hovered)
{
    if (!hovered) {
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int levelX = static_cast<int>((mouse.x - imageMin.x) / zoom);
    const int levelY = static_cast<int>((mouse.y - imageMin.y) / zoom);
    uint16_t existingBlock = 0;
    if (!GetBlockAtLevelPoint(core, levelX, levelY, existingBlock)) {
        return;
    }

    const int blockX = levelX >> 5;
    const int blockY = levelY >> 5;
    const int width = (std::max)(1, state.blockBrushWidth);
    const int height = (std::max)(1, state.blockBrushHeight);
    const std::vector<uint16_t>& brush = state.blockBrush;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const uint16_t block = index < brush.size() ? brush[index] : state.selectedBlock;
            const ImVec2 previewMin(
                imageMin.x + static_cast<float>((blockX + x) * 32) * zoom,
                imageMin.y + static_cast<float>((blockY + y) * 32) * zoom);
            const ImVec2 previewMax(previewMin.x + 32.0f * zoom, previewMin.y + 32.0f * zoom);

            drawList->AddRectFilled(previewMin, previewMax, IM_COL32(255, 230, 70, 36));
            DrawBlockPreview(core, drawList, previewMin, block, zoom, 190);
            drawList->AddRect(previewMin, previewMax, IM_COL32(255, 230, 70, 230), 0.0f, 0, 2.0f);
        }
    }
}

void LevelRenderer::DrawEventSprite(SC4Core& core, const EventInfo& event, ImDrawList* drawList, ImVec2 imageMin, float zoom)
{
    if (core.type != 0 || core.region != 0) {
        return;
    }

    if (event.type == EVENT_TYPE_CANDLE) {
        const unsigned candleOffset = ReadWordAt(core, 0x81A654);
        if (candleOffset != 0) {
            DrawSpriteAssembly(core, drawList, imageMin, zoom, (event.xpos & 0x3FFC), (event.ypos & 0x3FFC), candleOffset, 0);
        }
        
        const unsigned dropId = event.eventId & 0x3F;
        if (dropId >= 0x16) {
            const unsigned dropOffset = ReadWordAt(core, 0x81A654 + ((dropId - 0x16) << 1));
            if (dropOffset != 0) {
                DrawSpriteAssembly(core, drawList, imageMin, zoom, (event.xpos & 0x3FFC), (event.ypos & 0x3FFC), dropOffset, 0);
            }
        }
        return;
    }

    if (event.type != EVENT_TYPE_ENEMY && event.type != EVENT_TYPE_OBJECT) {
        return;
    }

    const unsigned assemblyOffset = EventAssemblyOffset(event);
    if (assemblyOffset == 0) {
        return;
    }

    unsigned slotOffset = 0;
    if (!TryGetSpriteSlotOffset(core, event, slotOffset)) {
        return;
    }

    DrawSpriteAssembly(core, drawList, imageMin, zoom, (event.xpos & 0x3FFC), (event.ypos & 0x3FFC), assemblyOffset, slotOffset);
}

void LevelRenderer::DrawEventThumbnail(SC4Core& core, ImDrawList* drawList, ImVec2 min, ImVec2 max, const EventInfo& event)
{
    struct PreviewBounds {
        bool valid = false;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    auto extendBounds = [](PreviewBounds& bounds, int x, int y, int w, int h) {
        if (!bounds.valid) {
            bounds.left = x;
            bounds.top = y;
            bounds.right = x + w;
            bounds.bottom = y + h;
            bounds.valid = true;
            return;
        }
        bounds.left = (std::min)(bounds.left, x);
        bounds.top = (std::min)(bounds.top, y);
        bounds.right = (std::max)(bounds.right, x + w);
        bounds.bottom = (std::max)(bounds.bottom, y + h);
    };

    auto measureAssembly = [&](unsigned assemblyOffset, unsigned slotOffset, PreviewBounds& bounds) {
        const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(0x840000 | (assemblyOffset & 0xFFFF)));
        if (!CanReadRom(core, pcOffset, 1)) {
            return;
        }

        const BYTE* tileBase = core.rom + pcOffset;
        const unsigned tileCount = *tileBase++;
        if (tileCount > 0x80 || !CanReadRom(core, pcOffset + 1, tileCount * 4)) {
            return;
        }

        for (unsigned i = 0; i < tileCount; ++i) {
            const signed char xRel = static_cast<signed char>(*tileBase++);
            const signed char yRel = static_cast<signed char>(*tileBase++);
            const WORD map = *reinterpret_cast<const WORD*>(tileBase);
            tileBase += 2;

            const unsigned info = (map >> 8) & 0xFF;
            const bool largeSprite = (info & 0x20) != 0;
            for (unsigned j = 0; j < (largeSprite ? 4u : 1u); ++j) {
                const int xOffset = static_cast<int>(j % 2) * 8;
                const int yOffset = static_cast<int>(j / 2) * 8;
                extendBounds(bounds, static_cast<int>(xRel) + xOffset, static_cast<int>(yRel) + yOffset, 8, 8);
            }
        }
    };

    PreviewBounds bounds = {};
    if (event.type == EVENT_TYPE_CANDLE) {
        const unsigned candleOffset = ReadWordAt(core, 0x81A654);
        if (candleOffset != 0) {
            measureAssembly(candleOffset, 0, bounds);
        }

        const unsigned dropId = event.eventId & 0x3F;
        if (dropId >= 0x16) {
            const unsigned dropOffset = ReadWordAt(core, 0x81A654 + ((dropId - 0x16) << 1));
            if (dropOffset != 0) {
                measureAssembly(dropOffset, 0, bounds);
            }
        }
    } else if (event.type == EVENT_TYPE_ENEMY || event.type == EVENT_TYPE_OBJECT) {
        const unsigned assemblyOffset = EventAssemblyOffset(event);
        unsigned slotOffset = 0;
        if (assemblyOffset != 0 && TryGetSpriteSlotOffset(core, event, slotOffset)) {
            measureAssembly(assemblyOffset, slotOffset, bounds);
        }
    } else {
        return;
    }

    if (!bounds.valid || bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        DrawEventPlaceholder(drawList, min, max, event);
        return;
    }

    const float width = static_cast<float>(bounds.right - bounds.left);
    const float height = static_cast<float>(bounds.bottom - bounds.top);
    const float availW = (std::max)(1.0f, max.x - min.x);
    const float availH = (std::max)(1.0f, max.y - min.y);
    const float scale = (std::min)(availW / width, availH / height);
    if (scale <= 0.0f) {
        return;
    }

    const float drawnW = width * scale;
    const float drawnH = height * scale;
    const ImVec2 origin(
        min.x + (availW - drawnW) * 0.5f,
        min.y + (availH - drawnH) * 0.5f);

    const ImU32 bgA = IM_COL32(24, 24, 28, 255);
    const ImU32 bgB = IM_COL32(40, 40, 46, 255);
    const float checker = (std::max)(2.0f, scale);
    for (float y = min.y; y < max.y; y += checker) {
        for (float x = min.x; x < max.x; x += checker) {
            const bool light = (((int)((x - min.x) / checker) + (int)((y - min.y) / checker)) & 1) != 0;
            drawList->AddRectFilled(
                ImVec2(x, y),
                ImVec2((std::min)(x + checker, max.x), (std::min)(y + checker, max.y)),
                light ? bgB : bgA);
        }
    }

    if (event.type == EVENT_TYPE_CANDLE) {
        const unsigned candleOffset = ReadWordAt(core, 0x81A654);
        if (candleOffset != 0) {
            DrawSpriteAssembly(core, drawList, origin, scale, -bounds.left, -bounds.top, candleOffset, 0);
        }
        const unsigned dropId = event.eventId & 0x3F;
        if (dropId >= 0x16) {
            const unsigned dropOffset = ReadWordAt(core, 0x81A654 + ((dropId - 0x16) << 1));
            if (dropOffset != 0) {
                DrawSpriteAssembly(core, drawList, origin, scale, -bounds.left, -bounds.top, dropOffset, 0);
            }
        }
    } else {
        unsigned slotOffset = 0;
        if (TryGetSpriteSlotOffset(core, event, slotOffset)) {
            DrawSpriteAssembly(core, drawList, origin, scale, -bounds.left, -bounds.top, EventAssemblyOffset(event), slotOffset);
        }
    }

    drawList->AddRect(min, max, IM_COL32(92, 92, 102, 255));
}

void LevelRenderer::DrawSpriteAssembly(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, int originX, int originY, unsigned assemblyOffset, unsigned slotOffset)
{
    const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(0x840000 | (assemblyOffset & 0xFFFF)));
    if (!CanReadRom(core, pcOffset, 1)) {
        return;
    }

    const BYTE* tileBase = core.rom + pcOffset;
    const unsigned tileCount = *tileBase++;
    if (tileCount > 0x80 || !CanReadRom(core, pcOffset + 1, tileCount * 4)) {
        return;
    }

    for (unsigned i = 0; i < tileCount; ++i) {
        const signed char xRel = static_cast<signed char>(*tileBase++);
        const signed char yRel = static_cast<signed char>(*tileBase++);
        const WORD map = *reinterpret_cast<const WORD*>(tileBase);
        tileBase += 2;

        const unsigned tile = (map & 0xFF) + slotOffset;
        const unsigned info = (map >> 8) & 0xFF;
        const bool largeSprite = (info & 0x20) != 0;
        const bool flipX = ((info >> 6) & 0x1) != 0;
        const bool flipY = ((info >> 7) & 0x1) != 0;
        const unsigned palette = (info >> 1) & 0x7;

        for (unsigned j = 0; j < (largeSprite ? 4u : 1u); ++j) {
            const int xOffset = static_cast<int>(j % 2) * 8;
            const int yOffset = static_cast<int>(j / 2) * 8;
            const unsigned tileOffset = largeSprite ? (j ^ (flipX ? 0x1u : 0x0u) ^ (flipY ? 0x2u : 0x0u)) : j;
            const unsigned actualTile = tile + (tileOffset % 2) + (tileOffset / 2) * 16;
            DrawSpriteTile(core, drawList, imageMin, zoom, originX + xRel + xOffset, originY + yRel + yOffset, actualTile, palette, flipX, flipY);
        }
    }
}

void LevelRenderer::DrawSpriteTile(SC4Core& core, ImDrawList* drawList, ImVec2 imageMin, float zoom, int dstX, int dstY, unsigned tile, unsigned palette, bool flipX, bool flipY)
{
    const unsigned vramByteAddr = core.type == 0 ? 0xC000u : core.type == 1 ? 0x0000u : 0x4000u;
    const unsigned tileIndex = tile & 0x3FFu;
    const BYTE* image = core.vramCache + vramByteAddr * 2 + (tileIndex << 6);
    const float pixelSize = zoom;

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int srcX = flipX ? 7 - x : x;
            const int srcY = flipY ? 7 - y : y;
            const BYTE pixel = image[srcY * 8 + srcX];
            if ((pixel & 0x0F) == 0) {
                continue;
            }

            const uint32_t color = ConvertColor(core.palCache[pixel | 0x80 | ((palette & 0x7) << 4)]);
            const ImU32 imColor = IM_COL32(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, 245);
            const float x0 = imageMin.x + static_cast<float>(dstX + x) * zoom;
            const float y0 = imageMin.y + static_cast<float>(dstY + y) * zoom;
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pixelSize, y0 + pixelSize), imColor);
        }
    }
}

void LevelRenderer::DrawBlockPreview(SC4Core& core, ImDrawList* drawList, ImVec2 pos, uint16_t block, float scale, ImU8 alpha) const
{
    uint16_t displayBlock = block;
    if (core.type == 1) {
        displayBlock = static_cast<WORD>(((displayBlock & 0x8000) >> 1) | ((displayBlock & 0x4000) << 1) | ((displayBlock & ~0xC000) >> 5));
    } else if (core.type == 2) {
        displayBlock = static_cast<WORD>(((displayBlock & 0x0080) << 7) | (displayBlock & ~0xFF00));
    }

    const unsigned blockOffset = GetBlockOffset(core, displayBlock);
    const bool flipX = (displayBlock & 0x8000) != 0;
    const bool flipY = (displayBlock & 0x4000) != 0;
    for (int tileY = 0; tileY < 4; ++tileY) {
        for (int tileX = 0; tileX < 4; ++tileX) {
            const int srcTileX = flipX ? 3 - tileX : tileX;
            const int srcTileY = flipY ? 3 - tileY : tileY;
            const unsigned tileOffset = (srcTileX << 1) + (srcTileY << 3);
            uint16_t tile = *reinterpret_cast<const uint16_t*>(core.ram + blockOffset + tileOffset);
            if (flipX) {
                tile ^= 0x4000u;
            }
            if (flipY) {
                tile ^= 0x8000u;
            }
            DrawTilePreview(core, drawList, ImVec2(pos.x + tileX * 8.0f * scale, pos.y + tileY * 8.0f * scale), tile, scale, alpha);
        }
    }
}

void LevelRenderer::DrawTilePreview(SC4Core& core, ImDrawList* drawList, ImVec2 pos, uint16_t tile, float scale, ImU8 alpha, int paletteOverride) const
{
    BYTE palette = static_cast<BYTE>(((tile >> 10) & 0x7) << 4);
    unsigned tileIndex = tile & 0x3FF;
    if (core.isMode7()) {
        tileIndex = tile & 0xFF;
        palette = 0;
    } else if (paletteOverride >= 0) {
        palette = static_cast<BYTE>((paletteOverride & 0xF) << 4);
    }

    const BYTE* image = core.vramCache + core.GetTileVramByteAddr() * 2 + (tileIndex << 6);
    const bool flipX = (tile & 0x4000) != 0;
    const bool flipY = (tile & 0x8000) != 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int srcX = flipX ? 7 - x : x;
            const int srcY = flipY ? 7 - y : y;
            const BYTE colorIndex = image[srcY * 8 + srcX] | palette;
            const uint32_t color = ConvertColor(core.palCache[colorIndex]);
            drawList->AddRectFilled(
                ImVec2(pos.x + x * scale, pos.y + y * scale),
                ImVec2(pos.x + (x + 1) * scale, pos.y + (y + 1) * scale),
                IM_COL32(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, alpha));
        }
    }
}

unsigned LevelRenderer::GetBlockOffset(const SC4Core& core, uint16_t block) const
{
    const unsigned blockNum = block & (core.numBlocks - 1);
    if (core.type == 0) {
        return (blockNum << 5) + 0x2000 + core.mapBase;
    }
    if (core.type == 1) {
        return (blockNum << 5) + 0x1000 + core.mapBase;
    }
    if (core.type == 2) {
        return (blockNum << 5) + 0x3000 + core.mapBase;
    }
    return 0;
}

bool LevelRenderer::GetBlockAtLevelPoint(SC4Core& core, int levelX, int levelY, uint16_t& block) const
{
    if (levelX < 0 || levelY < 0) {
        return false;
    }

    const int blockX = levelX >> 5;
    const int blockY = levelY >> 5;
    if (blockX < 0 || blockY < 0 || blockX >= static_cast<int>(core.levelWidth) * 8 || blockY >= static_cast<int>(core.levelHeight) * 8) {
        return false;
    }

    const int sceneX = blockX >> 3;
    const int sceneY = blockY >> 3;
    const int localX = blockX & 0x7;
    const int localY = blockY & 0x7;
    const int index = ((sceneY * core.levelWidth + sceneX) << 6) + (localY << 3) + localX;
    block = core.mapping[index];
    return true;
}

bool LevelRenderer::SetBlockAtLevelPoint(SC4Core& core, int levelX, int levelY, uint16_t block)
{
    if (levelX < 0 || levelY < 0) {
        return false;
    }

    const int blockX = levelX >> 5;
    const int blockY = levelY >> 5;
    if (blockX < 0 || blockY < 0 || blockX >= static_cast<int>(core.levelWidth) * 8 || blockY >= static_cast<int>(core.levelHeight) * 8) {
        return false;
    }

    const int sceneX = blockX >> 3;
    const int sceneY = blockY >> 3;
    const int localX = blockX & 0x7;
    const int localY = blockY & 0x7;
    const int sceneIndex = sceneY * core.levelWidth + sceneX;
    const int localIndex = (localY << 3) + localX;
    const int mappingIndex = (sceneIndex << 6) + localIndex;
    if (core.mapping[mappingIndex] == block) {
        return false;
    }

    core.mapping[mappingIndex] = block;
    if (core.type == 0) {
        const unsigned blockIndex = static_cast<unsigned>(sceneIndex) * 0x80 + static_cast<unsigned>(localIndex) * 2 + core.mapBase;
        const unsigned ramOffset = 0x4000 + blockIndex;
        *reinterpret_cast<WORD*>(core.ram + ramOffset) = block;
        WriteExpandedRamToRom(core, ramOffset, 2);
    } else if (core.type == 1) {
        const unsigned blockIndex = static_cast<unsigned>(sceneIndex) * 0x80 + static_cast<unsigned>(localIndex) * 2 + core.mapBase;
        const unsigned ramOffset = 0x5000 + blockIndex;
        *reinterpret_cast<WORD*>(core.ram + ramOffset) = block;
        WriteExpandedRamToRom(core, ramOffset, 2);
    } else if (core.type == 2) {
        const unsigned blockIndex = static_cast<unsigned>(sceneIndex) * 0x40 + static_cast<unsigned>(localIndex) + core.mapBase;
        const unsigned ramOffset = 0x4000 + blockIndex;
        *(core.ram + ramOffset) = static_cast<BYTE>(block & 0xFF);
        WriteExpandedRamToRom(core, ramOffset, 1);
    }

    return true;
}

int LevelRenderer::HitTestEvent(SC4Core& core, int levelX, int levelY) const
{
    int hitIndex = -1;
    int bestDistanceSq = 14 * 14;
    int eventIndex = 0;
    for (const auto& event : core.eventTable) {
        const int dx = levelX - static_cast<int>(event.xpos & 0x3FFC);
        const int dy = levelY - static_cast<int>(event.ypos & 0x3FFC);
        const int distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            hitIndex = eventIndex;
        }
        ++eventIndex;
    }
    return hitIndex;
}

EventInfo* LevelRenderer::EventByIndex(SC4Core& core, int eventIndex) const
{
    if (eventIndex < 0 || eventIndex >= static_cast<int>(core.eventTable.size())) {
        return nullptr;
    }

    auto iter = core.eventTable.begin();
    std::advance(iter, eventIndex);
    return &*iter;
}

ImU32 LevelRenderer::CollisionColor(uint16_t collisionType) const
{
    switch (collisionType) {
    case 0xCA:
    case 0xCE:
    case 0xD0:
        return IM_COL32(70, 150, 255, 90);
    case 0xCC:
    case 0xE0:
        return IM_COL32(255, 205, 80, 95);
    case 0xDA:
    case 0xDC:
    case 0xDE:
    case 0xD8:
        return IM_COL32(125, 230, 125, 95);
    case 0xE2:
        return IM_COL32(255, 70, 75, 120);
    case 0xE4:
        return IM_COL32(155, 120, 255, 80);
    default:
        return IM_COL32(255, 120, 190, 80);
    }
}

void LevelRenderer::RenderTile(SC4Core& core, int dstX, int dstY, uint16_t tile)
{
    BYTE palette = static_cast<BYTE>((tile >> 6) & 0x70);
    unsigned tileIndex = tile & 0x3FF;
    if (core.isMode7()) {
        tileIndex = tile & 0xFF;
        palette = 0;
    }

    const BYTE* image = core.vramCache + core.GetTileVramByteAddr() * 2 + (tileIndex << 6);
    const bool flipX = (tile & 0x4000) != 0;
    const bool flipY = (tile & 0x8000) != 0;

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int srcX = flipX ? 7 - x : x;
            const int srcY = flipY ? 7 - y : y;
            const BYTE colorIndex = image[srcY * 8 + srcX] | palette;
            PutPixel(dstX + x, dstY + y, ConvertColor(core.palCache[colorIndex]));
        }
    }
}

uint32_t LevelRenderer::ConvertColor(uint16_t color) const
{
    const uint8_t r = static_cast<uint8_t>(((color >> 10) & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x1F) * 255 / 31);
    const uint8_t b = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
    return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
}

void LevelRenderer::PutPixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= textureWidth_ || y >= textureHeight_) {
        return;
    }
    pixels_[static_cast<size_t>(y) * static_cast<size_t>(textureWidth_) + static_cast<size_t>(x)] = color;
}
