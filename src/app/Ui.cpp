#include "Ui.h"

#include "BpsPatch.h"
#include "EventDragDrop.h"
#include "EventNames.h"
#include "EditorUndo.h"
#include "HudEditor.h"
#include "InstrumentEditor.h"
#include "MusicEditor.h"
#include "PropertyPanel.h"
#include "SpriteEditor.h"
#include "Emulator.h"
#include "SC4Core.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <commdlg.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <climits>
#include <ctime>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include <new>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>
#include <string>

static bool g_resetDefaultDockLayout = false;
static std::vector<std::string> g_logMessages;

struct ScratchImage {
    std::wstring path;
    std::string name;
    ID3D11ShaderResourceView* texture = nullptr;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    ImVec2 pos = ImVec2(20.0f, 20.0f);
    float scale = 1.0f;
    int uniqueColorCount = -1;
};

static std::vector<ScratchImage> g_scratchImages;
static int g_selectedScratchImage = -1;
static int g_scratchPaletteTarget = 0;
static int g_scratchTileTarget = 0;
static std::array<bool, 8> g_scratchImportPalettes = { false, false, true, true, true, false, false, false };
static ImVec2 g_scratchSelectStart = ImVec2(0.0f, 0.0f);
static ImVec2 g_scratchSelectEnd = ImVec2(0.0f, 0.0f);
static bool g_scratchSelectingPixels = false;
static ID3D11Texture2D* g_emulatorTexture = nullptr;
static ID3D11ShaderResourceView* g_emulatorTextureView = nullptr;
static bool g_internalEmulatorCapturesKeyboard = false;

static void AddLog(const std::string& message)
{
    g_logMessages.push_back(message);
    if (g_logMessages.size() > 200) {
        g_logMessages.erase(g_logMessages.begin(), g_logMessages.begin() + (g_logMessages.size() - 200));
    }
}

static std::string CurrentTimeString()
{
    std::time_t now = std::time(nullptr);
    std::tm localTime = {};
    localtime_s(&localTime, &now);

    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

static std::string SaveLogMessage(const std::string& path)
{
    return "ROM saved: " + path + " at " + CurrentTimeString();
}

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>((std::max)(0, length - 1)), '\0');
    if (length > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), length, nullptr, nullptr);
    }
    return out;
}

static std::string FileNameFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return WideToUtf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
}

static bool HasPngExtension(const std::wstring& path)
{
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return ext == L".png";
}

static std::string OpenRomDialog(HWND owner)
{
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Open Super Castlevania IV ROM";
    if (!GetOpenFileNameA(&ofn)) {
        return {};
    }
    return fileName;
}

static std::string SaveRomDialog(HWND owner, const std::string& currentPath)
{
    char fileName[MAX_PATH] = {};
    if (!currentPath.empty()) {
        strncpy_s(fileName, currentPath.c_str(), _TRUNCATE);
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Save ROM As";
    ofn.lpstrDefExt = "sfc";
    if (!GetSaveFileNameA(&ofn)) {
        return {};
    }
    return fileName;
}

static std::string OpenOriginalRomDialog(HWND owner)
{
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Select Original Unmodified ROM";
    if (!GetOpenFileNameA(&ofn)) {
        return {};
    }
    return fileName;
}

static std::string SaveBpsDialog(HWND owner, const std::string& currentRomPath)
{
    char fileName[MAX_PATH] = {};
    if (!currentRomPath.empty()) {
        strncpy_s(fileName, currentRomPath.c_str(), _TRUNCATE);
        char* extension = strrchr(fileName, '.');
        if (extension) {
            strcpy_s(extension, MAX_PATH - static_cast<size_t>(extension - fileName), ".bps");
        } else {
            strcat_s(fileName, ".bps");
        }
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "BPS patch (*.bps)\0*.bps\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Export BPS Patch";
    ofn.lpstrDefExt = "bps";
    if (!GetSaveFileNameA(&ofn)) {
        return {};
    }
    return fileName;
}

bool ConfirmUnsavedChanges(EditorState& state, HWND hwnd, const char* action)
{
    if (!state.session.IsDirty()) {
        return true;
    }

    const std::string message = "The ROM has unsaved changes.\n\nSave before "
        + std::string(action ? action : "continuing") + "?";
    const int choice = MessageBoxA(hwnd, message.c_str(), "SC4Ed ImGui", MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDCANCEL || choice == 0) {
        return false;
    }
    if (choice == IDNO) {
        return true;
    }
    if (state.session.Save()) {
        AddLog(SaveLogMessage(state.session.Info().path));
        return true;
    }

    const std::string error = "Could not save the ROM:\n\n" + state.session.LastError();
    MessageBoxA(hwnd, error.c_str(), "Save failed", MB_OK | MB_ICONERROR);
    return false;
}

static unsigned ScancodeForVirtualKey(unsigned vk)
{
    return MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16;
}

static void EnsureDefaultInternalEmulatorControls()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    const auto assignKey = [](InternalEmulatorButtonType button, unsigned vk) {
        InternalEmulatorButtonSetting& setting = set.emulatorButtons[static_cast<unsigned>(button)];
        if (setting.value == 0) {
            setting.type = InternalEmulatorInputType::KEY;
            setting.value = ScancodeForVirtualKey(vk);
        }
    };

    assignKey(InternalEmulatorButtonType::UP, VK_UP);
    assignKey(InternalEmulatorButtonType::DOWN, VK_DOWN);
    assignKey(InternalEmulatorButtonType::LEFT, VK_LEFT);
    assignKey(InternalEmulatorButtonType::RIGHT, VK_RIGHT);
    assignKey(InternalEmulatorButtonType::B, 'Z');
    assignKey(InternalEmulatorButtonType::Y, 'X');
    assignKey(InternalEmulatorButtonType::A, 'C');
    assignKey(InternalEmulatorButtonType::X, 'S');
    assignKey(InternalEmulatorButtonType::L, 'A');
    assignKey(InternalEmulatorButtonType::R, 'D');
    assignKey(InternalEmulatorButtonType::SELECT, VK_RSHIFT);
    assignKey(InternalEmulatorButtonType::START, VK_RETURN);
    assignKey(InternalEmulatorButtonType::NEXTSTATE, VK_OEM_6);
    assignKey(InternalEmulatorButtonType::PREVSTATE, VK_OEM_4);
    assignKey(InternalEmulatorButtonType::SAVESTATE, VK_F5);
    assignKey(InternalEmulatorButtonType::LOADSTATE, VK_F8);
}

static void ReleaseInternalEmulatorTexture()
{
    if (g_emulatorTextureView) {
        g_emulatorTextureView->Release();
        g_emulatorTextureView = nullptr;
    }
    if (g_emulatorTexture) {
        g_emulatorTexture->Release();
        g_emulatorTexture = nullptr;
    }
}

static bool EnsureInternalEmulatorTexture(ID3D11Device* device)
{
    if (g_emulatorTexture && g_emulatorTextureView) {
        return true;
    }
    if (!device) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 256;
    desc.Height = 224;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_emulatorTexture))) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(g_emulatorTexture, &viewDesc, &g_emulatorTextureView))) {
        ReleaseInternalEmulatorTexture();
        return false;
    }
    return true;
}

static bool StartInternalEmulator(EditorState& state, HWND hwnd)
{
    if (!state.session.IsLoaded()) {
        AddLog("Open a ROM before starting the internal emulator.");
        return false;
    }

    EnsureDefaultInternalEmulatorControls();
    hWID[0] = hwnd;

    SC4Core& core = state.session.Core();
    nmmx.type = core.type;
    nmmx.region = core.region;
    nmmx.level = static_cast<WORD>(state.level);
    nmmx.point = static_cast<WORD>(state.checkpoint);
    strncpy_s(nmmx.filePath, state.session.Info().path.c_str(), _TRUNCATE);

    if (!Emulator::Instance()->IsInitialized() && !Emulator::Instance()->Init()) {
        AddLog("Internal emulator init failed: " + Emulator::Instance()->retroLoadError);
        return false;
    }

    const std::vector<BYTE> romBackup(core.rom, core.rom + core.romSize);
    const std::set<unsigned> spriteUpdateBackup = core.spriteUpdate;
    const std::set<unsigned> simonSpriteUpdateBackup = core.simonSpriteUpdate;
    core.SaveEvents();
    core.SaveLevel();
    const bool stagedAllSprites = core.spriteUpdate.empty() && core.simonSpriteUpdate.empty();
    const bool loadedRom = stagedAllSprites && Emulator::Instance()->LoadRom(core.rom, core.romSize);
    std::memcpy(core.rom, romBackup.data(), romBackup.size());
    core.spriteUpdate = spriteUpdateBackup;
    core.simonSpriteUpdate = simonSpriteUpdateBackup;

    if (!stagedAllSprites) {
        Emulator::Instance()->Terminate();
        AddLog("Internal emulator reload failed: pending sprite graphics require an expanded ROM.");
        return false;
    }
    if (!loadedRom) {
        AddLog("Internal emulator ROM load failed: " + Emulator::Instance()->retroLoadError);
        return false;
    }
    if (!Emulator::Instance()->LoadLevel(state.level)) {
        AddLog("Internal emulator level load failed: " + Emulator::Instance()->retroLoadError);
        return false;
    }

    state.internalEmulatorRunning = true;
    state.showInternalEmulator = true;
    AddLog("Internal emulator started.");
    return true;
}

static void StopInternalEmulator(EditorState& state)
{
    Emulator::Instance()->Stop();
    state.internalEmulatorRunning = false;
    state.hasInternalEmulatorCamera = false;
    AddLog("Internal emulator stopped.");
}

void ApplySc4Style()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.23f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.43f, 0.48f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.23f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.42f, 0.47f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.54f, 0.58f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.33f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.30f, 0.25f, 1.00f);
}

static void DrawTopBar(EditorState& state, HWND hwnd)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open ROM...")) {
                const std::string path = OpenRomDialog(hwnd);
                if (!path.empty() && ConfirmUnsavedChanges(state, hwnd, "opening another ROM")) {
                    if (state.session.OpenRom(path)) {
                        state.level = 0;
                        state.checkpoint = 0;
                        state.selectedEventIndex = -1;
                        state.undoStack.clear();
                        state.redoStack.clear();
                        state.levelRenderer.Invalidate();
                        AddLog("Loaded ROM: " + path);
                    } else {
                        AddLog("Open failed: " + state.session.LastError());
                    }
                }
            }
            ImGui::BeginDisabled(!state.session.IsLoaded());
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                AddLog(state.session.Save() ? SaveLogMessage(state.session.Info().path) : "Save failed: " + state.session.LastError());
            }
            if (ImGui::MenuItem("Save As...")) {
                const std::string path = SaveRomDialog(hwnd, state.session.IsLoaded() ? state.session.Info().path : "");
                if (!path.empty()) {
                    AddLog(state.session.SaveAs(path) ? SaveLogMessage(state.session.Info().path) : "Save As failed: " + state.session.LastError());
                }
            }
            if (ImGui::MenuItem("Export BPS Patch...")) {
                const std::string originalPath = OpenOriginalRomDialog(hwnd);
                if (!originalPath.empty()) {
                    const std::string patchPath = SaveBpsDialog(hwnd, state.session.Info().path);
                    if (!patchPath.empty()) {
                        std::string error;
                        const std::vector<uint8_t> target = state.session.CurrentRomBytes();
                        if (ExportBpsPatch(originalPath, target, patchPath, error)) {
                            AddLog("BPS patch exported: " + patchPath + " at " + CurrentTimeString());
                        } else {
                            AddLog("BPS export failed: " + error);
                            MessageBoxA(hwnd, error.c_str(), "BPS export failed", MB_OK | MB_ICONERROR);
                        }
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("ROM Expander...")) {
                if (state.session.ExpandRom()) {
                    state.session.LoadLevel(state.level, state.checkpoint);
                    state.session.LoadCurrentLayer(state.showBackground);
                    state.levelRenderer.Invalidate();
                    state.selectedEventIndex = -1;
                    state.undoStack.clear();
                    state.redoStack.clear();
                    AddLog("ROM expanded.");
                } else {
                    AddLog("Expand failed: " + state.session.LastError());
                }
            }
            ImGui::Separator();
            ImGui::BeginDisabled(!state.session.IsLoaded());
            if (ImGui::MenuItem(state.internalEmulatorRunning ? "Restart Internal Emulator" : "Run Internal Emulator")) {
                StartInternalEmulator(state, hwnd);
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!state.internalEmulatorRunning);
            if (ImGui::MenuItem("Stop Internal Emulator")) {
                StopInternalEmulator(state);
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::BeginDisabled(state.undoStack.empty());
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                PerformUndo(state);
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(state.redoStack.empty());
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                PerformRedo(state);
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Show Background", nullptr, &state.showBackground)) {
                if (state.session.LoadCurrentLayer(state.showBackground)) {
                    state.levelRenderer.Invalidate();
                    state.undoStack.clear();
                    state.redoStack.clear();
                }
            }
            ImGui::MenuItem("Show Collision", nullptr, &state.showCollision);
            ImGui::MenuItem("Show Events", nullptr, &state.showEvents);
            ImGui::MenuItem("Help", nullptr, &state.showHelp);
            if (ImGui::MenuItem("Reset Default Layout")) {
                g_resetDefaultDockLayout = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Help View", nullptr, &state.showHelp);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

static void BuildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 dockspaceSize)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

    ImGuiID mainId = dockspaceId;
    const ImGuiID rightId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.26f, nullptr, &mainId);
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Up, 0.80f, nullptr, &mainId);
    const ImGuiID bottomRightId = ImGui::DockBuilderSplitNode(bottomId, ImGuiDir_Right, 0.26f, nullptr, &mainId);
 
    ImGui::DockBuilderDockWindow("Level View", mainId);
    ImGui::DockBuilderDockWindow("Palette", bottomRightId);
    ImGui::DockBuilderDockWindow("ROM", mainId);
    ImGui::DockBuilderDockWindow("Level Properties", rightId);
    ImGui::DockBuilderDockWindow("Navigator", bottomId);
    ImGui::DockBuilderDockWindow("Tools", rightId);
    ImGui::DockBuilderDockWindow("Log", mainId);
    ImGui::DockBuilderDockWindow("Global Properties", rightId);
    ImGui::DockBuilderDockWindow("Selection", bottomRightId);
    ImGui::DockBuilderDockWindow("Internal Emulator", bottomId);
    ImGui::DockBuilderDockWindow("Help###HelpView", mainId);
    ImGui::DockBuilderFinish(dockspaceId);


//   ImGuiID mainId = dockspaceId;
//   const ImGuiID bottomId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.30f, nullptr, &mainId);
//   ImGuiID leftId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.22f, nullptr, &mainId);
//   const ImGuiID rightId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.26f, nullptr, &mainId);
//   const ImGuiID paletteId = ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Up, 0.24f, nullptr, &leftId);
//   ImGuiID leftTopId = leftId;
//   const ImGuiID leftBottomId = ImGui::DockBuilderSplitNode(leftTopId, ImGuiDir_Down, 0.56f, nullptr, &leftTopId);
//
//   ImGui::DockBuilderDockWindow("Level View", mainId);
//   ImGui::DockBuilderDockWindow("Palette", paletteId);
//   ImGui::DockBuilderDockWindow("ROM", leftTopId);
//   ImGui::DockBuilderDockWindow("Level Properties", leftTopId);
//   ImGui::DockBuilderDockWindow("Navigator", leftBottomId);
//   ImGuiID bottomTopId = bottomId;
//   const ImGuiID logId = ImGui::DockBuilderSplitNode(bottomTopId, ImGuiDir_Down, 0.18f, nullptr, &bottomTopId);
//   ImGui::DockBuilderDockWindow("Tools", bottomTopId);
//   ImGui::DockBuilderDockWindow("Log", logId);
//   ImGui::DockBuilderDockWindow("Global Properties", rightId);
//   ImGui::DockBuilderDockWindow("Selection", rightId);
//   ImGui::DockBuilderDockWindow("Internal Emulator", rightId);
//   ImGui::DockBuilderDockWindow("Help###HelpView", rightId);
//   ImGui::DockBuilderFinish(dockspaceId);
}

static int g_selectedPaletteIndex = 0;
static int g_palettePickerIndex = -1;
static ImVec4 g_palettePickerColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

static ImVec4 PaletteColorToImVec4(SNESCore& core, int index)
{
    const WORD color = core.palCache[index];
    return ImVec4(
        ((color >> 10) & 0x1F) / 31.0f,
        ((color >> 5) & 0x1F) / 31.0f,
        (color & 0x1F) / 31.0f,
        1.0f);
}

static void AddPaletteScriptOffsets(SC4Core& core, DWORD addr, int targetIndex, std::set<DWORD>& offsets)
{
    if (!core.rom) {
        return;
    }

    const DWORD pcAddr = SNESCore::snes2pc(static_cast<int>(addr));
    if (pcAddr >= core.romSize || *(core.rom + pcAddr) == 0) {
        return;
    }

    const BYTE packetType = *(core.rom + pcAddr);
    addr += packetType == 4 ? 0 : 1;
    if (packetType != 1) {
        return;
    }

    addr += 2;
    const DWORD sourceBank = (core.type == 0 ? 0x86u : core.type == 1 ? 0x88u : 0x84u) << 16;
    while (true) {
        const DWORD entryPc = SNESCore::snes2pc(static_cast<int>(addr));
        if (entryPc + 4 > core.romSize) {
            return;
        }

        const WORD srcOffset = *reinterpret_cast<WORD*>(core.rom + entryPc);
        if (srcOffset == 0) {
            return;
        }
        addr += 2;

        const DWORD dstPc = SNESCore::snes2pc(static_cast<int>(addr));
        if (dstPc + 2 > core.romSize) {
            return;
        }
        const WORD dstOffset = *reinterpret_cast<WORD*>(core.rom + dstPc);
        addr += 2;

        const int palIndex = (static_cast<int>(dstOffset) - 0x2200) / 2;
        const DWORD srcAddr = sourceBank | srcOffset;
        const DWORD srcPc = SNESCore::snes2pc(static_cast<int>(srcAddr));
        if (srcPc + 2 > core.romSize) {
            continue;
        }

        const WORD size = static_cast<WORD>(*reinterpret_cast<WORD*>(core.rom + srcPc) + 1);
        const int localIndex = targetIndex - palIndex;
        if (localIndex >= 0 && localIndex < size / 2) {
            const DWORD colorPc = SNESCore::snes2pc(static_cast<int>(srcAddr + 2 + localIndex * 2));
            if (colorPc + 2 <= core.romSize) {
                offsets.insert(colorPc);
            }
        }
    }
}

static std::set<DWORD> PaletteSourceOffsetsForIndex(SC4Core& core, int index)
{
    std::set<DWORD> offsets;
    if (index < 0 || index >= 0x100 || !core.rom) {
        return offsets;
    }

    if (core.palCacheOffset[index] != 0) {
        offsets.insert(core.palCacheOffset[index]);
    }

    std::vector<DWORD> addrList;
    if (core.type == 0) {
        addrList.push_back(0x818715);
        addrList.push_back((0x81 << 16) | *reinterpret_cast<WORD*>(core.rom + SNESCore::snes2pc(0x8693E7 + core.level * 2)));
        addrList.push_back((0x81 << 16) | *reinterpret_cast<WORD*>(core.rom + SNESCore::snes2pc(0x86946F + core.level * 2)));
    } else if (core.type == 1) {
        addrList.push_back(0x858AC7);
        const BYTE levelPalette = *(core.rom + SNESCore::snes2pc(0x8589CC + (core.level + 1)));
        addrList.push_back((0x85 << 16) | *reinterpret_cast<WORD*>(core.rom + SNESCore::snes2pc(0x858A21 + levelPalette)));
    } else if (core.type == 2) {
        addrList.push_back(0x818B6E);
        addrList.push_back(0x818B9D);
        addrList.push_back(0x818C19);
        addrList.push_back(0x818BF2);
        addrList.push_back((0x81 << 16) | *reinterpret_cast<WORD*>(core.rom + SNESCore::snes2pc(0x81A21A + core.level * 2)));
    }
    addrList.insert(addrList.end(), core.dynPalTable.begin(), core.dynPalTable.end());

    for (const DWORD addr : addrList) {
        AddPaletteScriptOffsets(core, addr, index, offsets);
    }
    return offsets;
}

static void WritePaletteColor(EditorState& state, int index, const ImVec4& color)
{
    if (!state.session.IsLoaded()) {
        return;
    }

    SC4Core& core = state.session.Core();
    const WORD r = static_cast<WORD>(std::clamp(color.x, 0.0f, 1.0f) * 31.0f + 0.5f);
    const WORD g = static_cast<WORD>(std::clamp(color.y, 0.0f, 1.0f) * 31.0f + 0.5f);
    const WORD b = static_cast<WORD>(std::clamp(color.z, 0.0f, 1.0f) * 31.0f + 0.5f);
    const WORD cacheColor = static_cast<WORD>((r << 10) | (g << 5) | b);
    const WORD romColor = core.Convert16Color(cacheColor);

    core.palCache[index] = cacheColor;
    const std::set<DWORD> pcOffsets = PaletteSourceOffsetsForIndex(core, index);
    for (const DWORD pcOffset : pcOffsets) {
        state.session.WriteRomPc(pcOffset, 2, romColor);
    }
    state.levelRenderer.Invalidate();
}

static void DrawPalettePanel(EditorState& state)
{
    ImGui::Begin("Palette");

    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("No ROM loaded");
        ImGui::End();
        return;
    }

    SC4Core& core = state.session.Core();
    g_selectedPaletteIndex = std::clamp(g_selectedPaletteIndex, 0, 0xFF);

    ImGui::TextUnformatted("Click a swatch to edit it.");
    ImGui::Separator();

    ImGui::BeginChild("palette-list", ImVec2(0.0f, 260.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (int palette = 0; palette < 16; ++palette) {
        char label[32];
        std::snprintf(label, sizeof(label), "Palette %d", palette);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        for (int color = 0; color < 16; ++color) {
            const int index = palette * 16 + color;
            const ImVec4 colorVec = PaletteColorToImVec4(core, index);
            ImGui::PushID(index);
            if (ImGui::ColorButton("##swatch", colorVec, ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f))) {
                g_selectedPaletteIndex = index;
            }
            if (ImGui::IsItemClicked()) {
                g_selectedPaletteIndex = index;
            }
            if (ImGui::IsItemHovered()) {
                const std::set<DWORD> offsets = PaletteSourceOffsetsForIndex(core, index);
                ImGui::SetTooltip("Palette %d Color %d\nROM %08X%s\nSNES %04X",
                    palette,
                    color,
                    static_cast<unsigned>(core.palCacheOffset[index]),
                    offsets.size() > 1 ? " (mirrored)" : "",
                    core.palCache[index]);
            }
            if (color != 15) {
                ImGui::SameLine(0.0f, 1.8f);
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();

    ImGui::Separator();
    const int paletteIndex = g_selectedPaletteIndex / 16;
    const int colorIndex = g_selectedPaletteIndex % 16;
    const DWORD pcOffset = core.palCacheOffset[g_selectedPaletteIndex];
    const WORD snesColor = core.palCache[g_selectedPaletteIndex];
    ImGui::Text("Selected: Palette %d Color %d", paletteIndex, colorIndex);
    ImGui::Text("ROM offset: %08X", static_cast<unsigned>(pcOffset));
    ImGui::Text("SNES color: %04X", snesColor);

    if (g_palettePickerIndex != g_selectedPaletteIndex) {
        g_palettePickerIndex = g_selectedPaletteIndex;
        g_palettePickerColor = PaletteColorToImVec4(core, g_selectedPaletteIndex);
    }
    if (ImGui::ColorPicker3("##palette-picker", reinterpret_cast<float*>(&g_palettePickerColor), ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
        WritePaletteColor(state, g_selectedPaletteIndex, g_palettePickerColor);
    }

    ImGui::End();
}

static void DrawSidebar(EditorState& state)
{
    ImGui::Begin("ROM");
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("No ROM loaded");
        if (!state.session.LastError().empty()) {
            ImGui::TextWrapped("%s", state.session.LastError().c_str());
        }
    } else {
        const RomInfo& rom = state.session.Info();
        ImGui::TextWrapped("%s", rom.path.c_str());
        ImGui::Separator();
        ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(rom.size));
        ImGui::Text("Header: %s", rom.hasHeader ? "512 byte copier header" : "none");
        ImGui::Text("CRC32: %08X", rom.checksum);
        ImGui::Text("Level size: %dx%d scenes", state.session.LevelWidth(), state.session.LevelHeight());
    }
    ImGui::End();

    ImGui::Begin("Navigator");
    const int maxLevel = state.session.IsLoaded() ? state.session.LevelCount() - 1 : 13;
    if (ImGui::SliderInt("Level", &state.level, 0, maxLevel)) {
        if (state.session.IsLoaded() && state.session.LoadLevel(state.level, state.checkpoint)) {
            state.session.LoadCurrentLayer(state.showBackground);
            state.levelRenderer.Invalidate();
            state.selectedEventIndex = -1;
            state.undoStack.clear();
            state.redoStack.clear();
        }
    }
    if (ImGui::SliderInt("Checkpoint", &state.checkpoint, 0, 15)) {
        if (state.session.IsLoaded() && state.session.LoadLevel(state.level, state.checkpoint)) {
            state.session.LoadCurrentLayer(state.showBackground);
            state.levelRenderer.Invalidate();
            state.selectedEventIndex = -1;
            state.undoStack.clear();
            state.redoStack.clear();
        }
    }
    ImGui::SliderFloat("Zoom", &state.zoom, 1.0f, 4.0f, "%.1fx");
    int paintBlock = (static_cast<int>(state.selectedBlock) & 0x3ffu);
    ImGui::SetNextItemWidth(96.0f);
    if (ImGui::InputInt("Paint block", &paintBlock, 1, 10, ImGuiInputTextFlags_AutoSelectAll)) {
        state.selectedBlock = static_cast<uint16_t>(std::clamp(paintBlock, 0, 0xFFFF));
    }
    if (ImGui::Checkbox("Show Background", &state.showBackground)) {
        if (state.session.LoadCurrentLayer(state.showBackground)) {
            state.levelRenderer.Invalidate();
            state.undoStack.clear();
            state.redoStack.clear();
        }
    }
    ImGui::Checkbox("Show Collision", &state.showCollision);
    ImGui::Checkbox("Show Events", &state.showEvents);
    ImGui::Checkbox("Show Grid", &state.showGrid);
    ImGui::End();
}

static void DrawViewport(EditorState& state, ID3D11Device* device)
{
    ImGui::Begin("Level View");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (state.session.IsLoaded() && state.levelRenderer.EnsureTexture(device, state.session)) {
        state.levelRenderer.Draw(avail, state);
    } else {
        ImGui::BeginChild("empty-level-view", avail);
        ImGui::TextUnformatted("Open a ROM to render the level here.");
        ImGui::EndChild();
    }
    ImGui::End();
}

static void DrawInternalEmulator(EditorState& state, ID3D11Device* device)
{
    if (!state.showInternalEmulator) {
        return;
    }

    if (!ImGui::Begin("Internal Emulator", &state.showInternalEmulator)) {
        ImGui::End();
        return;
    }

    const bool running = state.internalEmulatorRunning && Emulator::Instance()->GetState() != Emulator::EmuState::OFF;
    if (running) {
        if (ImGui::Button(Emulator::Instance()->GetState() == Emulator::EmuState::PAUSE ? "Resume" : "Pause")) {
            Emulator::Instance()->Pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Edits")) {
            StartInternalEmulator(state, hWID[0]);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            StopInternalEmulator(state);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Follow Camera", &state.followInternalEmulatorCamera);
        ImGui::SameLine();
        ImGui::TextDisabled("Controls: Arrows, Z/X/C/S, A/D, Enter, Right Shift");
    } else {
        state.internalEmulatorRunning = false;
        ImGui::TextUnformatted("Internal emulator is not running.");
        ImGui::BeginDisabled(!state.session.IsLoaded());
        if (ImGui::Button("Run Internal Emulator")) {
            StartInternalEmulator(state, hWID[0]);
        }
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }

    FrameState frame = {};
    Emulator::Instance()->GetFrameState(frame);
    state.hasInternalEmulatorCamera = true;
    state.internalEmulatorCameraX = frame.xpos;
    state.internalEmulatorCameraY = frame.ypos;
    if (!frame.buffer || !EnsureInternalEmulatorTexture(device)) {
        ImGui::TextUnformatted("Waiting for emulator frame...");
        ImGui::End();
        return;
    }

    std::array<uint32_t, 256 * 224> pixels = {};
    for (size_t i = 0; i < pixels.size(); ++i) {
        const uint32_t src = frame.buffer[i];
        const uint32_t r = (src >> 16) & 0xFF;
        const uint32_t g = (src >> 8) & 0xFF;
        const uint32_t b = src & 0xFF;
        pixels[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (context) {
        context->UpdateSubresource(g_emulatorTexture, 0, nullptr, pixels.data(), 256 * sizeof(uint32_t), 0);
        context->Release();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale = (std::max)(1.0f, std::floor((std::min)(avail.x / 256.0f, avail.y / 224.0f)));
    const ImVec2 imageSize(256.0f * scale, 224.0f * scale);
    ImGui::Image(reinterpret_cast<ImTextureID>(g_emulatorTextureView), imageSize);
    g_internalEmulatorCapturesKeyboard = ImGui::IsItemHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::Text("Level %d  Camera %d,%d  State %04X/%04X",
        frame.levelNum,
        frame.xpos,
        frame.ypos,
        frame.state0,
        frame.state1);

    ImGui::End();
}

static void ReleaseScratchImage(ScratchImage& image)
{
    if (image.texture) {
        image.texture->Release();
        image.texture = nullptr;
    }
    image.pixels.clear();
}

static bool CreateScratchTexture(ID3D11Device* device, ScratchImage& image)
{
    if (!device || image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return false;
    }

    ID3D11Texture2D* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(image.width);
    desc.Height = static_cast<UINT>(image.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = image.pixels.data();
    data.SysMemPitch = static_cast<UINT>(image.width * 4);
    HRESULT hr = device->CreateTexture2D(&desc, &data, &texture);
    if (SUCCEEDED(hr)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(texture, &srvDesc, &image.texture);
    }
    if (texture) {
        texture->Release();
    }
    if (FAILED(hr)) {
        ReleaseScratchImage(image);
        return false;
    }
    return true;
}

static bool LoadPngTexture(ID3D11Device* device, const std::wstring& path, ScratchImage& image)
{
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr)) {
        hr = converter->GetSize(&width, &height);
    }
    if (SUCCEEDED(hr) && (width == 0 || height == 0 || width > 16384 || height > 16384)) {
        hr = E_INVALIDARG;
    }

    std::vector<uint8_t> pixels;
    if (SUCCEEDED(hr)) {
        const UINT stride = width * 4;
        pixels.resize(static_cast<size_t>(stride) * static_cast<size_t>(height));
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    if (FAILED(hr)) {
        ReleaseScratchImage(image);
        return false;
    }

    image.path = path;
    image.name = FileNameFromPath(path);
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels = std::move(pixels);
    image.scale = 1.0f;
    const float offset = 24.0f * static_cast<float>(g_scratchImages.size() % 10);
    image.pos = ImVec2(20.0f + offset, 20.0f + offset);
    return CreateScratchTexture(device, image);
}

static void ProcessScratchDrops(ID3D11Device* device, const std::vector<std::wstring>& droppedFiles)
{
    for (const std::wstring& path : droppedFiles) {
        if (!HasPngExtension(path)) {
            AddLog("Scratch Board ignored non-PNG: " + WideToUtf8(path));
            continue;
        }

        ScratchImage image;
        if (LoadPngTexture(device, path, image)) {
            g_scratchImages.push_back(image);
            g_selectedScratchImage = static_cast<int>(g_scratchImages.size()) - 1;
            AddLog("Scratch Board added PNG: " + image.name);
        } else {
            AddLog("Scratch Board failed to load PNG: " + WideToUtf8(path));
        }
    }
}

static uint64_t HashTile8x8(const ScratchImage& image, int tileX, int tileY)
{
    uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int px = tileX * 8 + x;
            const int py = tileY * 8 + y;
            uint8_t rgba[4] = {};
            if (px >= 0 && py >= 0 && px < image.width && py < image.height) {
                const size_t src = (static_cast<size_t>(py) * static_cast<size_t>(image.width) + static_cast<size_t>(px)) * 4u;
                rgba[0] = image.pixels[src + 0];
                rgba[1] = image.pixels[src + 1];
                rgba[2] = image.pixels[src + 2];
                rgba[3] = image.pixels[src + 3];
            }
            for (uint8_t value : rgba) {
                hash ^= value;
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

static bool TilesEqual8x8(const ScratchImage& image, int ax, int ay, int bx, int by)
{
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int apx = ax * 8 + x;
            const int apy = ay * 8 + y;
            const int bpx = bx * 8 + x;
            const int bpy = by * 8 + y;
            uint8_t a[4] = {};
            uint8_t b[4] = {};
            if (apx >= 0 && apy >= 0 && apx < image.width && apy < image.height) {
                const size_t src = (static_cast<size_t>(apy) * static_cast<size_t>(image.width) + static_cast<size_t>(apx)) * 4u;
                std::memcpy(a, image.pixels.data() + src, 4);
            }
            if (bpx >= 0 && bpy >= 0 && bpx < image.width && bpy < image.height) {
                const size_t src = (static_cast<size_t>(bpy) * static_cast<size_t>(image.width) + static_cast<size_t>(bpx)) * 4u;
                std::memcpy(b, image.pixels.data() + src, 4);
            }
            if (std::memcmp(a, b, 4) != 0) {
                return false;
            }
        }
    }
    return true;
}

static void ScratchReadPixel(const ScratchImage& image, int x, int y, uint8_t rgba[4])
{
    rgba[0] = 0;
    rgba[1] = 0;
    rgba[2] = 0;
    rgba[3] = 0;
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return;
    }
    const size_t src = (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4u;
    std::memcpy(rgba, image.pixels.data() + src, 4);
}

static bool ScratchTileIsTransparent8x8(const ScratchImage& image, int tileX, int tileY)
{
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            uint8_t rgba[4] = {};
            ScratchReadPixel(image, tileX * 8 + x, tileY * 8 + y, rgba);
            if (rgba[3] >= 128) {
                return false;
            }
        }
    }
    return true;
}

static bool ConvertScratchImageToTileset(ID3D11Device* device, int imageIndex)
{
    if (imageIndex < 0 || imageIndex >= static_cast<int>(g_scratchImages.size())) {
        return false;
    }

    const ScratchImage& source = g_scratchImages[imageIndex];
    const std::string sourceName = source.name;
    if (source.width < 8 || source.height < 8 || source.pixels.empty()) {
        AddLog("Scratch Board tileset conversion failed: source is smaller than 8x8.");
        return false;
    }

    const int tilesX = source.width / 8;
    const int tilesY = source.height / 8;
    constexpr size_t maxTextureDimension = 16384;
    constexpr size_t maxTilesetColumns = 128;
    constexpr size_t maxUniqueTiles = (maxTextureDimension / 8u) * maxTilesetColumns;
    std::vector<ImVec2> uniqueTiles;
    std::unordered_map<uint64_t, std::vector<int>> hashBuckets;
    uniqueTiles.reserve((std::min)(static_cast<size_t>(tilesX) * static_cast<size_t>(tilesY), maxUniqueTiles));

    for (int y = 0; y < tilesY; ++y) {
        for (int x = 0; x < tilesX; ++x) {
            const uint64_t hash = HashTile8x8(source, x, y);
            bool found = false;
            auto& bucket = hashBuckets[hash];
            for (int uniqueIndex : bucket) {
                const ImVec2 existing = uniqueTiles[uniqueIndex];
                if (TilesEqual8x8(source, x, y, static_cast<int>(existing.x), static_cast<int>(existing.y))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (uniqueTiles.size() >= maxUniqueTiles) {
                    AddLog("Scratch Board tileset conversion failed: too many unique 8x8 tiles for one texture.");
                    return false;
                }
                bucket.push_back(static_cast<int>(uniqueTiles.size()));
                uniqueTiles.push_back(ImVec2(static_cast<float>(x), static_cast<float>(y)));
            }
        }
    }

    if (uniqueTiles.empty()) {
        AddLog("Scratch Board tileset conversion failed: no complete 8x8 tiles found.");
        return false;
    }

    const size_t tileCount = uniqueTiles.size();
    const size_t squareColumns = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(tileCount))));
    const size_t columns = std::clamp(squareColumns, size_t(16), maxTilesetColumns);
    const size_t outWidthSize = (std::min)(columns, tileCount) * 8u;
    const size_t outHeightSize = ((tileCount + columns - 1u) / columns) * 8u;
    constexpr size_t maxOutputBytes = 256ull * 1024ull * 1024ull;
    if (outWidthSize == 0 || outHeightSize == 0 || outWidthSize > maxTextureDimension || outHeightSize > maxTextureDimension ||
        outWidthSize > SIZE_MAX / outHeightSize / 4u || outWidthSize * outHeightSize * 4u > maxOutputBytes) {
        AddLog("Scratch Board tileset conversion failed: generated tileset is too large.");
        return false;
    }

    const int outWidth = static_cast<int>(outWidthSize);
    const int outHeight = static_cast<int>(outHeightSize);
    ScratchImage tileset;
    tileset.name = sourceName + " tileset";
    tileset.width = outWidth;
    tileset.height = outHeight;
    try {
        tileset.pixels.assign(outWidthSize * outHeightSize * 4u, 0);
    } catch (const std::bad_alloc&) {
        AddLog("Scratch Board tileset conversion failed: not enough memory for generated tileset.");
        return false;
    }

    for (int i = 0; i < static_cast<int>(uniqueTiles.size()); ++i) {
        const int srcTileX = static_cast<int>(uniqueTiles[i].x);
        const int srcTileY = static_cast<int>(uniqueTiles[i].y);
        const int dstTileX = i % static_cast<int>(columns);
        const int dstTileY = i / static_cast<int>(columns);
        for (int y = 0; y < 8; ++y) {
            const size_t src = (static_cast<size_t>(srcTileY * 8 + y) * static_cast<size_t>(source.width) + static_cast<size_t>(srcTileX * 8)) * 4u;
            const size_t dst = (static_cast<size_t>(dstTileY * 8 + y) * static_cast<size_t>(outWidth) + static_cast<size_t>(dstTileX * 8)) * 4u;
            std::memcpy(tileset.pixels.data() + dst, source.pixels.data() + src, 8u * 4u);
        }
    }

    tileset.scale = 2.0f;
    tileset.pos = ImVec2(source.pos.x + 40.0f, source.pos.y + 40.0f);
    if (!CreateScratchTexture(device, tileset)) {
        AddLog("Scratch Board tileset conversion failed: could not create texture.");
        return false;
    }

    try {
        g_scratchImages.push_back(std::move(tileset));
    } catch (const std::bad_alloc&) {
        AddLog("Scratch Board tileset conversion failed: not enough memory to add generated tileset.");
        ReleaseScratchImage(tileset);
        return false;
    }
    g_selectedScratchImage = static_cast<int>(g_scratchImages.size()) - 1;
    AddLog("Scratch Board converted " + sourceName + " to " + std::to_string(uniqueTiles.size()) + " unique 8x8 tiles.");
    return true;
}

static int CountScratchUniqueColors(const ScratchImage& image)
{
    if (image.pixels.empty()) {
        return 0;
    }

    std::unordered_map<uint32_t, bool> colors;
    colors.reserve((std::min)(static_cast<size_t>(image.width) * static_cast<size_t>(image.height), size_t(65536)));
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        const uint32_t rgba =
            (static_cast<uint32_t>(image.pixels[i + 0]) << 24) |
            (static_cast<uint32_t>(image.pixels[i + 1]) << 16) |
            (static_cast<uint32_t>(image.pixels[i + 2]) << 8) |
            static_cast<uint32_t>(image.pixels[i + 3]);
        colors[rgba] = true;
    }

    return static_cast<int>(colors.size());
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

static unsigned ScratchBlockOffset(const SC4Core& core, uint16_t block)
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

static unsigned ScratchLevelTileVramByteAddr(const SC4Core& core)
{
    return core.type == 0 ? 0xC000u : core.type == 1 ? 0x0000u : 0x4000u;
}

static unsigned ScratchTileRomKey(SC4Core& core)
{
    return core.GetTileVramByteAddr() >> 1;
}

static unsigned ScratchWritableTileCount(SC4Core& core)
{
    if (!core.expandedROM || !core.expandedOffset.count(core.level)) {
        return 0;
    }

    const auto levelIt = core.expandedOffset.find(core.level);
    if (levelIt == core.expandedOffset.end()) {
        return 0;
    }
    const auto tileIt = levelIt->second.find(ScratchTileRomKey(core));
    if (tileIt == levelIt->second.end()) {
        return 0;
    }

    const unsigned maxTiles = core.isMode7() ? 0x100u : 0x400u;
    if (core.isMode7()) {
        return maxTiles;
    }

    const unsigned bytesPerTile = 32u;
    return (std::min)(maxTiles, tileIt->second.second / bytesPerTile);
}

static bool SetScratchLevelBlock(SC4Core& core, int blockX, int blockY, uint16_t block)
{
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

static std::vector<bool> CollectScratchForegroundUsedTiles(EditorState& state)
{
    std::vector<bool> used(0x400, false);
    if (!state.session.IsLoaded()) {
        return used;
    }

    SC4Core& core = state.session.Core();
    const int levelBlocksX = static_cast<int>(core.levelWidth) * 8;
    const int levelBlocksY = static_cast<int>(core.levelHeight) * 8;
    std::vector<bool> usedBlocks(core.numBlocks, false);
    for (int blockY = 0; blockY < levelBlocksY; ++blockY) {
        for (int blockX = 0; blockX < levelBlocksX; ++blockX) {
            const int sceneX = blockX >> 3;
            const int sceneY = blockY >> 3;
            const int localX = blockX & 0x7;
            const int localY = blockY & 0x7;
            const int sceneIndex = sceneY * core.levelWidth + sceneX;
            const int localIndex = (localY << 3) + localX;
            const uint16_t block = core.mapping[(sceneIndex << 6) + localIndex] & static_cast<uint16_t>(core.numBlocks - 1);
            if (block < usedBlocks.size()) {
                usedBlocks[block] = true;
            }
        }
    }

    for (unsigned block = 0; block < core.numBlocks; ++block) {
        if (!usedBlocks[block]) {
            continue;
        }

        const unsigned blockOffset = ScratchBlockOffset(core, static_cast<uint16_t>(block));
        const WORD* blockTiles = reinterpret_cast<const WORD*>(core.ram + blockOffset);
        for (int cell = 0; cell < 16; ++cell) {
            const unsigned tile = blockTiles[cell] & 0x3FF;
            if (tile < used.size()) {
                used[tile] = true;
            }
        }
    }

    return used;
}

struct ScratchColorCluster {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    int count = 0;
};

struct ScratchTilePalettePlan {
    std::vector<int> tilePalette;
    int paletteCount = 0;
};

static double ClusterDistanceSq(const ScratchColorCluster& a, const ScratchColorCluster& b)
{
    const double dr = a.r - b.r;
    const double dg = a.g - b.g;
    const double db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

static ImVec4 ClusterToColor(const ScratchColorCluster& cluster)
{
    return ImVec4(
        static_cast<float>(cluster.r / 255.0),
        static_cast<float>(cluster.g / 255.0),
        static_cast<float>(cluster.b / 255.0),
        1.0f);
}

static void ReduceClustersToSnesPalette(std::vector<ScratchColorCluster>& clusters)
{
    while (clusters.size() > 15) {
        int bestA = 0;
        int bestB = 1;
        double bestDistance = ClusterDistanceSq(clusters[0], clusters[1]);
        for (int a = 0; a < static_cast<int>(clusters.size()); ++a) {
            for (int b = a + 1; b < static_cast<int>(clusters.size()); ++b) {
                const double distance = ClusterDistanceSq(clusters[a], clusters[b]);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestA = a;
                    bestB = b;
                }
            }
        }

        ScratchColorCluster& a = clusters[bestA];
        const ScratchColorCluster& b = clusters[bestB];
        const int combined = a.count + b.count;
        a.r = (a.r * a.count + b.r * b.count) / combined;
        a.g = (a.g * a.count + b.g * b.count) / combined;
        a.b = (a.b * a.count + b.b * b.count) / combined;
        a.count = combined;
        clusters.erase(clusters.begin() + bestB);
    }

    std::sort(clusters.begin(), clusters.end(), [](const ScratchColorCluster& a, const ScratchColorCluster& b) {
        return a.count > b.count;
    });
}

static bool ApplyScratchPaletteFromSelection(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        AddLog("Scratch Board palette failed: load a ROM first.");
        return false;
    }
    if (g_selectedScratchImage < 0 || g_selectedScratchImage >= static_cast<int>(g_scratchImages.size())) {
        AddLog("Scratch Board palette failed: select an image first.");
        return false;
    }

    const ScratchImage& image = g_scratchImages[g_selectedScratchImage];
    if (image.pixels.empty()) {
        AddLog("Scratch Board palette failed: selected image has no pixels.");
        return false;
    }

    int left = static_cast<int>(std::floor((std::min)(g_scratchSelectStart.x, g_scratchSelectEnd.x)));
    int right = static_cast<int>(std::ceil((std::max)(g_scratchSelectStart.x, g_scratchSelectEnd.x)));
    int top = static_cast<int>(std::floor((std::min)(g_scratchSelectStart.y, g_scratchSelectEnd.y)));
    int bottom = static_cast<int>(std::ceil((std::max)(g_scratchSelectStart.y, g_scratchSelectEnd.y)));
    if (right <= left || bottom <= top) {
        left = 0;
        top = 0;
        right = image.width;
        bottom = image.height;
    }
    left = std::clamp(left, 0, image.width);
    right = std::clamp(right, 0, image.width);
    top = std::clamp(top, 0, image.height);
    bottom = std::clamp(bottom, 0, image.height);
    if (right <= left || bottom <= top) {
        AddLog("Scratch Board palette failed: empty pixel selection.");
        return false;
    }

    std::unordered_map<uint32_t, int> colorCounts;
    bool hasTransparent = false;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4u;
            const uint8_t r = image.pixels[index + 0];
            const uint8_t g = image.pixels[index + 1];
            const uint8_t b = image.pixels[index + 2];
            const uint8_t a = image.pixels[index + 3];
            if (a < 128) {
                hasTransparent = true;
                continue;
            }
            const uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
            ++colorCounts[key];
        }
    }

    if (colorCounts.empty()) {
        AddLog("Scratch Board palette failed: selection only contains transparent pixels.");
        return false;
    }

    std::vector<ScratchColorCluster> clusters;
    clusters.reserve(colorCounts.size());
    for (const auto& [key, count] : colorCounts) {
        ScratchColorCluster cluster = {};
        cluster.r = static_cast<double>((key >> 16) & 0xFF);
        cluster.g = static_cast<double>((key >> 8) & 0xFF);
        cluster.b = static_cast<double>(key & 0xFF);
        cluster.count = count;
        clusters.push_back(cluster);
    }

    while (clusters.size() > 15) {
        int bestA = 0;
        int bestB = 1;
        double bestDistance = ClusterDistanceSq(clusters[0], clusters[1]);
        for (int a = 0; a < static_cast<int>(clusters.size()); ++a) {
            for (int b = a + 1; b < static_cast<int>(clusters.size()); ++b) {
                const double distance = ClusterDistanceSq(clusters[a], clusters[b]);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestA = a;
                    bestB = b;
                }
            }
        }

        ScratchColorCluster& a = clusters[bestA];
        const ScratchColorCluster& b = clusters[bestB];
        const int combined = a.count + b.count;
        a.r = (a.r * a.count + b.r * b.count) / combined;
        a.g = (a.g * a.count + b.g * b.count) / combined;
        a.b = (a.b * a.count + b.b * b.count) / combined;
        a.count = combined;
        clusters.erase(clusters.begin() + bestB);
    }

    std::sort(clusters.begin(), clusters.end(), [](const ScratchColorCluster& a, const ScratchColorCluster& b) {
        return a.count > b.count;
    });

    g_scratchPaletteTarget = std::clamp(g_scratchPaletteTarget, 0, 15);
    PushUndo(state);
    WritePaletteColor(state, g_scratchPaletteTarget * 16, ImVec4(0.0f, 0.0f, 0.0f, hasTransparent ? 0.0f : 1.0f));
    for (int i = 0; i < 15; ++i) {
        const int paletteIndex = g_scratchPaletteTarget * 16 + 1 + i;
        if (i < static_cast<int>(clusters.size())) {
            WritePaletteColor(state, paletteIndex, ClusterToColor(clusters[i]));
        } else {
            WritePaletteColor(state, paletteIndex, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }
    }
    AddLog("Scratch Board wrote " + std::to_string(clusters.size()) + " colors plus transparent to palette " + std::to_string(g_scratchPaletteTarget) + ".");
    return true;
}

static void GetScratchSelectionRect(const ScratchImage& image, int& left, int& top, int& right, int& bottom)
{
    left = static_cast<int>(std::floor((std::min)(g_scratchSelectStart.x, g_scratchSelectEnd.x)));
    right = static_cast<int>(std::ceil((std::max)(g_scratchSelectStart.x, g_scratchSelectEnd.x)));
    top = static_cast<int>(std::floor((std::min)(g_scratchSelectStart.y, g_scratchSelectEnd.y)));
    bottom = static_cast<int>(std::ceil((std::max)(g_scratchSelectStart.y, g_scratchSelectEnd.y)));
    if (right <= left || bottom <= top) {
        left = 0;
        top = 0;
        right = image.width;
        bottom = image.height;
    }
    left = std::clamp(left, 0, image.width);
    right = std::clamp(right, 0, image.width);
    top = std::clamp(top, 0, image.height);
    bottom = std::clamp(bottom, 0, image.height);
}

static BYTE NearestPaletteIndex(SC4Core& core, int palette, const uint8_t* rgba)
{
    if (rgba[3] < 128) {
        return 0;
    }

    const int r = rgba[0];
    const int g = rgba[1];
    const int b = rgba[2];
    const unsigned paletteRow = core.isMode7() ? 0 : ((palette & 0xF) << 4);
    BYTE bestIndex = 1;
    int bestDistance = INT_MAX;
    for (BYTE i = 1; i < 16; ++i) {
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
    return bestIndex;
}

static int NearestPaletteDistanceSq(SC4Core& core, int palette, const uint8_t* rgba)
{
    if (rgba[3] < 128) {
        return 0;
    }

    const int r = rgba[0];
    const int g = rgba[1];
    const int b = rgba[2];
    const unsigned paletteRow = core.isMode7() ? 0 : ((palette & 0xF) << 4);
    int bestDistance = INT_MAX;
    for (BYTE i = 1; i < 16; ++i) {
        const uint16_t palColor = core.palCache[i | paletteRow];
        const int pr = static_cast<int>(((palColor >> 10) & 0x1F) * 255 / 31);
        const int pg = static_cast<int>(((palColor >> 5) & 0x1F) * 255 / 31);
        const int pb = static_cast<int>((palColor & 0x1F) * 255 / 31);
        const int dr = r - pr;
        const int dg = g - pg;
        const int db = b - pb;
        bestDistance = (std::min)(bestDistance, dr * dr + dg * dg + db * db);
    }
    return bestDistance;
}

static uint32_t ScratchRgbKey(const uint8_t* rgba)
{
    return (static_cast<uint32_t>(rgba[0]) << 16) | (static_cast<uint32_t>(rgba[1]) << 8) | static_cast<uint32_t>(rgba[2]);
}

static std::vector<int> ScratchSelectedImportPalettes()
{
    std::vector<int> palettes;
    for (int palette = 0; palette < static_cast<int>(g_scratchImportPalettes.size()); ++palette) {
        if (g_scratchImportPalettes[palette]) {
            palettes.push_back(palette);
        }
    }
    return palettes;
}

static std::string ScratchPaletteList(const std::vector<int>& palettes, int count)
{
    std::string text;
    const int usedCount = (std::min)(count, static_cast<int>(palettes.size()));
    for (int i = 0; i < usedCount; ++i) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::to_string(palettes[i]);
    }
    return text;
}

static ScratchTilePalettePlan WriteScratchTilePalettes(EditorState& state, const ScratchImage& image, int sourceTileLeft, int sourceTileTop, int sourceTilesX, int sourceTilesY, const std::vector<int>& targetPalettes)
{
    struct PaletteGroup {
        std::unordered_map<uint32_t, int> colors;
    };

    const int maxPalettes = static_cast<int>(targetPalettes.size());

    ScratchTilePalettePlan plan = {};
    plan.tilePalette.assign(static_cast<size_t>(sourceTilesX) * static_cast<size_t>(sourceTilesY), targetPalettes.front());

    std::vector<PaletteGroup> groups;
    groups.reserve(maxPalettes);

    for (int tileY = 0; tileY < sourceTilesY; ++tileY) {
        for (int tileX = 0; tileX < sourceTilesX; ++tileX) {
            std::unordered_map<uint32_t, int> tileColors;
            const int srcBaseX = (sourceTileLeft + tileX) * 8;
            const int srcBaseY = (sourceTileTop + tileY) * 8;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    uint8_t rgba[4] = {};
                    ScratchReadPixel(image, srcBaseX + x, srcBaseY + y, rgba);
                    if (rgba[3] < 128) {
                        continue;
                    }
                    ++tileColors[ScratchRgbKey(rgba)];
                }
            }

            int bestGroup = -1;
            int bestUnion = INT_MAX;
            for (int groupIndex = 0; groupIndex < static_cast<int>(groups.size()); ++groupIndex) {
                int unionSize = static_cast<int>(groups[groupIndex].colors.size());
                for (const auto& [color, count] : tileColors) {
                    if (!groups[groupIndex].colors.count(color)) {
                        ++unionSize;
                    }
                }
                if (unionSize <= 15 && unionSize < bestUnion) {
                    bestUnion = unionSize;
                    bestGroup = groupIndex;
                }
            }

            if (bestGroup < 0 && static_cast<int>(groups.size()) < maxPalettes) {
                bestGroup = static_cast<int>(groups.size());
                groups.push_back({});
            }
            if (bestGroup < 0) {
                for (int groupIndex = 0; groupIndex < static_cast<int>(groups.size()); ++groupIndex) {
                    int unionSize = static_cast<int>(groups[groupIndex].colors.size());
                    for (const auto& [color, count] : tileColors) {
                        if (!groups[groupIndex].colors.count(color)) {
                            ++unionSize;
                        }
                    }
                    if (unionSize < bestUnion) {
                        bestUnion = unionSize;
                        bestGroup = groupIndex;
                    }
                }
            }
            if (bestGroup < 0) {
                bestGroup = 0;
                groups.push_back({});
            }

            for (const auto& [color, count] : tileColors) {
                groups[bestGroup].colors[color] += count;
            }
            plan.tilePalette[static_cast<size_t>(tileY) * static_cast<size_t>(sourceTilesX) + static_cast<size_t>(tileX)] = targetPalettes[bestGroup];
        }
    }

    plan.paletteCount = static_cast<int>(groups.size());
    for (int groupIndex = 0; groupIndex < static_cast<int>(targetPalettes.size()); ++groupIndex) {
        const int palette = targetPalettes[groupIndex];
        WritePaletteColor(state, palette * 16, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        std::vector<ScratchColorCluster> clusters;
        if (groupIndex < static_cast<int>(groups.size())) {
            clusters.reserve(groups[groupIndex].colors.size());
            for (const auto& [color, count] : groups[groupIndex].colors) {
                ScratchColorCluster cluster = {};
                cluster.r = static_cast<double>((color >> 16) & 0xFF);
                cluster.g = static_cast<double>((color >> 8) & 0xFF);
                cluster.b = static_cast<double>(color & 0xFF);
                cluster.count = count;
                clusters.push_back(cluster);
            }
            ReduceClustersToSnesPalette(clusters);
        }

        for (int i = 0; i < 15; ++i) {
            if (i < static_cast<int>(clusters.size())) {
                WritePaletteColor(state, palette * 16 + 1 + i, ClusterToColor(clusters[i]));
            } else {
                WritePaletteColor(state, palette * 16 + 1 + i, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            }
        }
    }

    return plan;
}

static ScratchTilePalettePlan PlanScratchTilePalettesFromExisting(SC4Core& core, const ScratchImage& image, int sourceTileLeft, int sourceTileTop, int sourceTilesX, int sourceTilesY)
{
    ScratchTilePalettePlan plan = {};
    plan.paletteCount = 16;
    plan.tilePalette.assign(static_cast<size_t>(sourceTilesX) * static_cast<size_t>(sourceTilesY), 0);

    for (int tileY = 0; tileY < sourceTilesY; ++tileY) {
        for (int tileX = 0; tileX < sourceTilesX; ++tileX) {
            const int srcBaseX = (sourceTileLeft + tileX) * 8;
            const int srcBaseY = (sourceTileTop + tileY) * 8;
            int bestPalette = 0;
            uint64_t bestDistance = UINT64_MAX;
            for (int palette = 0; palette < 16; ++palette) {
                uint64_t distance = 0;
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        uint8_t rgba[4] = {};
                        ScratchReadPixel(image, srcBaseX + x, srcBaseY + y, rgba);
                        distance += static_cast<uint64_t>(NearestPaletteDistanceSq(core, palette, rgba));
                    }
                }
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestPalette = palette;
                }
            }
            plan.tilePalette[static_cast<size_t>(tileY) * static_cast<size_t>(sourceTilesX) + static_cast<size_t>(tileX)] = bestPalette;
        }
    }

    return plan;
}

static bool CopyScratchTilesToRom(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        AddLog("Scratch Board tile copy failed: load a ROM first.");
        return false;
    }
    if (g_selectedScratchImage < 0 || g_selectedScratchImage >= static_cast<int>(g_scratchImages.size())) {
        AddLog("Scratch Board tile copy failed: select an image first.");
        return false;
    }

    SC4Core& core = state.session.Core();
    const unsigned tileVramByteAddr = ScratchLevelTileVramByteAddr(core);
    const unsigned tileRomKey = ScratchTileRomKey(core);
    if (!core.expandedROM || !core.expandedOffset.count(core.level) || !core.expandedOffset[core.level].count(tileRomKey)) {
        AddLog("Scratch Board tile copy failed: expand the ROM first.");
        return false;
    }

    const ScratchImage& image = g_scratchImages[g_selectedScratchImage];
    if (image.pixels.empty() || image.width < 8 || image.height < 8) {
        AddLog("Scratch Board tile copy failed: image has no complete 8x8 tiles.");
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    GetScratchSelectionRect(image, left, top, right, bottom);
    const int sourceTileLeft = left / 8;
    const int sourceTileTop = top / 8;
    const int sourceTileRight = (right + 7) / 8;
    const int sourceTileBottom = (bottom + 7) / 8;
    const int sourceTilesX = std::clamp(sourceTileRight, 0, image.width / 8) - std::clamp(sourceTileLeft, 0, image.width / 8);
    const int sourceTilesY = std::clamp(sourceTileBottom, 0, image.height / 8) - std::clamp(sourceTileTop, 0, image.height / 8);
    if (sourceTilesX <= 0 || sourceTilesY <= 0) {
        AddLog("Scratch Board tile copy failed: selection has no complete 8x8 tiles.");
        return false;
    }

    const unsigned romTileCount = ScratchWritableTileCount(core);
    if (romTileCount == 0) {
        AddLog("Scratch Board tile copy failed: no writable expanded tile region found.");
        return false;
    }
    g_scratchTileTarget = std::clamp(g_scratchTileTarget, 0, static_cast<int>(romTileCount) - 1);
    const unsigned tileBase = tileVramByteAddr * 2;
    const unsigned baseOffset = core.expandedOffset[core.level][tileRomKey].first;

    PushUndo(state);
    const std::vector<int> targetPalettes = { 2, 3, 4, 5, 6 };
    const ScratchTilePalettePlan palettePlan = WriteScratchTilePalettes(state, image, sourceTileLeft, sourceTileTop, sourceTilesX, sourceTilesY, targetPalettes);
    int copied = 0;
    for (int tileY = 0; tileY < sourceTilesY; ++tileY) {
        for (int tileX = 0; tileX < sourceTilesX; ++tileX) {
            const unsigned dstTile = static_cast<unsigned>(g_scratchTileTarget + copied);
            if (dstTile >= romTileCount) {
                break;
            }

            BYTE* raw = core.vramCache + tileBase + (dstTile << 6);
            const int srcBaseX = (sourceTileLeft + tileX) * 8;
            const int srcBaseY = (sourceTileTop + tileY) * 8;
            const int palette = palettePlan.tilePalette[static_cast<size_t>(tileY) * static_cast<size_t>(sourceTilesX) + static_cast<size_t>(tileX)];
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const size_t src = (static_cast<size_t>(srcBaseY + y) * static_cast<size_t>(image.width) + static_cast<size_t>(srcBaseX + x)) * 4u;
                    raw[x + y * 8] = NearestPaletteIndex(core, palette, image.pixels.data() + src);
                }
            }

            if (!core.isMode7()) {
                core.raw2tile4bpp(raw, core.rom + baseOffset + (dstTile << 5));
            } else {
                core.raw2tileMode7(raw, core.rom + baseOffset + (dstTile << 6));
            }
            ++copied;
        }
    }

    state.levelRenderer.Invalidate();
    AddLog("Scratch Board copied " + std::to_string(copied) + " tile(s) to ROM starting at tile " + std::to_string(g_scratchTileTarget) + " using palettes 2-" + std::to_string(1 + palettePlan.paletteCount) + ".");
    return copied > 0;
}

static bool ImportScratchImageAsLevel(EditorState& state, const char* targetName, const std::vector<int>& selectedPalettes, const std::vector<bool>* reservedTiles = nullptr, bool clearLayerFirst = false, bool allowMode7 = false)
{
    const std::string target = targetName ? targetName : "level";
    const std::string failPrefix = "Scratch Board " + target + " import failed: ";

    if (!state.session.IsLoaded()) {
        AddLog(failPrefix + "load a ROM first.");
        return false;
    }
    if (g_selectedScratchImage < 0 || g_selectedScratchImage >= static_cast<int>(g_scratchImages.size())) {
        AddLog(failPrefix + "select an image first.");
        return false;
    }

    SC4Core& core = state.session.Core();
    const bool mode7 = core.isMode7();
    if (mode7 && !allowMode7) {
        AddLog(failPrefix + "Mode 7 levels are not supported.");
        return false;
    }
    std::vector<int> targetPalettes = selectedPalettes;
    if (mode7) {
        targetPalettes = { 0 };
    } else if (targetPalettes.empty()) {
        AddLog(failPrefix + "select at least one target palette.");
        return false;
    }
    const unsigned tileVramByteAddr = ScratchLevelTileVramByteAddr(core);
    const unsigned tileRomKey = ScratchTileRomKey(core);
    if (!core.expandedROM || !core.expandedOffset.count(core.level) || !core.expandedOffset[core.level].count(tileRomKey)) {
        AddLog(failPrefix + "expand the ROM first.");
        return false;
    }

    const ScratchImage& image = g_scratchImages[g_selectedScratchImage];
    if (image.pixels.empty() || image.width < 32 || image.height < 32) {
        AddLog(failPrefix + "image needs complete 32x32 blocks.");
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    GetScratchSelectionRect(image, left, top, right, bottom);
    const int sourceTileLeft = left / 8;
    const int sourceTileTop = top / 8;
    const int sourceTileRight = (right + 7) / 8;
    const int sourceTileBottom = (bottom + 7) / 8;
    const int maxSourceTilesX = (image.width + 7) / 8;
    const int maxSourceTilesY = (image.height + 7) / 8;
    const int sourceTilesX = std::clamp(sourceTileRight, 0, maxSourceTilesX) - std::clamp(sourceTileLeft, 0, maxSourceTilesX);
    const int sourceTilesY = std::clamp(sourceTileBottom, 0, maxSourceTilesY) - std::clamp(sourceTileTop, 0, maxSourceTilesY);
    const int sourceBlocksX = (sourceTilesX + 3) / 4;
    const int sourceBlocksY = (sourceTilesY + 3) / 4;
    if (sourceBlocksX <= 0 || sourceBlocksY <= 0) {
        AddLog(failPrefix + "selection has no complete 32x32 blocks.");
        return false;
    }

    const int levelBlocksX = static_cast<int>(core.levelWidth) * 8;
    const int levelBlocksY = static_cast<int>(core.levelHeight) * 8;
    const int importBlocksX = (std::min)(sourceBlocksX, levelBlocksX);
    const int importBlocksY = (std::min)(sourceBlocksY, levelBlocksY);
    const int importTilesX = importBlocksX * 4;
    const int importTilesY = importBlocksY * 4;
    const unsigned romTileCount = ScratchWritableTileCount(core);
    if (romTileCount == 0) {
        AddLog(failPrefix + "no writable expanded tile region found.");
        return false;
    }
    g_scratchTileTarget = std::clamp(g_scratchTileTarget, 0, static_cast<int>(romTileCount) - 1);
    unsigned targetTile = static_cast<unsigned>(g_scratchTileTarget);
    if (core.numBlocks <= 1) {
        AddLog(failPrefix + "no editable block slots after reserved block 0.");
        return false;
    }
    const unsigned requestedBlock = state.selectedBlock & (core.numBlocks - 1);
    const unsigned targetBlock = (std::max)(1u, requestedBlock);

    PushUndo(state);
    const ScratchTilePalettePlan palettePlan = WriteScratchTilePalettes(
        state, image, sourceTileLeft, sourceTileTop, importTilesX, importTilesY, targetPalettes);

    struct TileRef {
        int x = 0;
        int y = 0;
        int palette = 0;
    };

    std::vector<TileRef> uniqueTiles;
    std::vector<int> tileToUnique(static_cast<size_t>(importTilesX) * static_cast<size_t>(importTilesY), -1);
    std::unordered_map<uint64_t, std::vector<int>> tileBuckets;
    for (int tileY = 0; tileY < importTilesY; ++tileY) {
        for (int tileX = 0; tileX < importTilesX; ++tileX) {
            const int absTileX = sourceTileLeft + tileX;
            const int absTileY = sourceTileTop + tileY;
            if (ScratchTileIsTransparent8x8(image, absTileX, absTileY)) {
                tileToUnique[static_cast<size_t>(tileY) * static_cast<size_t>(importTilesX) + static_cast<size_t>(tileX)] = -1;
                continue;
            }

            const int palette = palettePlan.tilePalette[static_cast<size_t>(tileY) * static_cast<size_t>(importTilesX) + static_cast<size_t>(tileX)];
            const uint64_t hash = HashTile8x8(image, absTileX, absTileY);
            int found = -1;
            auto& bucket = tileBuckets[hash];
            for (int uniqueIndex : bucket) {
                const TileRef& existing = uniqueTiles[uniqueIndex];
                if (TilesEqual8x8(image, absTileX, absTileY, existing.x, existing.y)) {
                    found = uniqueIndex;
                    break;
                }
            }
            if (found < 0) {
                found = static_cast<int>(uniqueTiles.size());
                bucket.push_back(found);
                uniqueTiles.push_back({ absTileX, absTileY, palette });
            }
            tileToUnique[static_cast<size_t>(tileY) * static_cast<size_t>(importTilesX) + static_cast<size_t>(tileX)] = found;
        }
    }

    const unsigned requestedTile = targetTile;
    if (reservedTiles && !uniqueTiles.empty()) {
        auto rangeIsFree = [&](unsigned start) {
            if (start + uniqueTiles.size() > romTileCount) {
                return false;
            }
            for (unsigned tile = start; tile < start + uniqueTiles.size(); ++tile) {
                if (tile < reservedTiles->size() && (*reservedTiles)[tile]) {
                    return false;
                }
            }
            return true;
        };

        if (!rangeIsFree(targetTile)) {
            bool found = false;
            for (unsigned start = 0; start + uniqueTiles.size() <= romTileCount; ++start) {
                if (rangeIsFree(start)) {
                    targetTile = start;
                    found = true;
                    break;
                }
            }
            if (!found) {
                AddLog(failPrefix + "not enough unused tile slots without touching foreground tiles.");
                return false;
            }
        }
    }
    g_scratchTileTarget = static_cast<int>(targetTile);

    if (targetTile + uniqueTiles.size() > romTileCount) {
        const unsigned availableTiles = targetTile < romTileCount ? romTileCount - targetTile : 0;
        AddLog(failPrefix + "not enough tile slots from target tile " + std::to_string(targetTile) +
            ": needed " + std::to_string(uniqueTiles.size()) +
            ", available " + std::to_string(availableTiles) +
            " (" + std::to_string(romTileCount) + " total writable).");
        return false;
    }

    std::vector<std::array<WORD, 16>> uniqueBlocks;
    std::vector<int> blockToUnique(static_cast<size_t>(importBlocksX) * static_cast<size_t>(importBlocksY), -1);
    for (int blockY = 0; blockY < importBlocksY; ++blockY) {
        for (int blockX = 0; blockX < importBlocksX; ++blockX) {
            std::array<WORD, 16> blockMaps = {};
            bool blockHasTile = false;
            for (int tileY = 0; tileY < 4; ++tileY) {
                for (int tileX = 0; tileX < 4; ++tileX) {
                    const int sourceX = blockX * 4 + tileX;
                    const int sourceY = blockY * 4 + tileY;
                    const int uniqueTile = tileToUnique[static_cast<size_t>(sourceY) * static_cast<size_t>(importTilesX) + static_cast<size_t>(sourceX)];
                    if (uniqueTile < 0) {
                        blockMaps[static_cast<size_t>(tileY * 4 + tileX)] = 0;
                        continue;
                    }

                    const TileRef& tileRef = uniqueTiles[static_cast<size_t>(uniqueTile)];
                    blockHasTile = true;
                    const WORD tileMap = static_cast<WORD>(targetTile + static_cast<unsigned>(uniqueTile));
                    blockMaps[static_cast<size_t>(tileY * 4 + tileX)] = mode7
                        ? tileMap
                        : static_cast<WORD>(tileMap | ((tileRef.palette & 0xF) << 10));
                }
            }

            int found = -1;
            if (blockHasTile) {
                for (int i = 0; i < static_cast<int>(uniqueBlocks.size()); ++i) {
                    if (uniqueBlocks[i] == blockMaps) {
                        found = i;
                        break;
                    }
                }
                if (found < 0) {
                    found = static_cast<int>(uniqueBlocks.size());
                    uniqueBlocks.push_back(blockMaps);
                }
            }
            blockToUnique[static_cast<size_t>(blockY) * static_cast<size_t>(importBlocksX) + static_cast<size_t>(blockX)] = found;
        }
    }

    if (targetBlock + uniqueBlocks.size() > core.numBlocks) {
        AddLog(failPrefix + "not enough block slots from block " + std::to_string(targetBlock) + ".");
        return false;
    }

    const unsigned tileBase = tileVramByteAddr * 2;
    const unsigned tileRomOffset = core.expandedOffset[core.level][tileRomKey].first;
    for (int i = 0; i < static_cast<int>(uniqueTiles.size()); ++i) {
        const unsigned dstTile = targetTile + static_cast<unsigned>(i);
        BYTE* raw = core.vramCache + tileBase + (dstTile << 6);
        const TileRef& tile = uniqueTiles[i];
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                uint8_t rgba[4] = {};
                ScratchReadPixel(image, tile.x * 8 + x, tile.y * 8 + y, rgba);
                raw[x + y * 8] = NearestPaletteIndex(core, tile.palette, rgba);
            }
        }
        if (mode7) {
            core.raw2tileMode7(raw, core.rom + tileRomOffset + (dstTile << 6));
        } else {
            core.raw2tile4bpp(raw, core.rom + tileRomOffset + (dstTile << 5));
            core.SetTileType(static_cast<WORD>(dstTile), 0xE4);
        }
    }

    for (int i = 0; i < static_cast<int>(uniqueBlocks.size()); ++i) {
        const uint16_t dstBlock = static_cast<uint16_t>(targetBlock + static_cast<unsigned>(i));
        const unsigned blockOffset = ScratchBlockOffset(core, dstBlock);
        WORD* blockTiles = reinterpret_cast<WORD*>(core.ram + blockOffset);
        for (int cell = 0; cell < 16; ++cell) {
            blockTiles[cell] = uniqueBlocks[i][static_cast<size_t>(cell)];
        }
        WriteExpandedRamToRom(core, blockOffset, 32);
    }

    if (clearLayerFirst) {
        for (int blockY = 0; blockY < levelBlocksY; ++blockY) {
            for (int blockX = 0; blockX < levelBlocksX; ++blockX) {
                SetScratchLevelBlock(core, blockX, blockY, 0);
            }
        }
    }

    for (int blockY = 0; blockY < importBlocksY; ++blockY) {
        for (int blockX = 0; blockX < importBlocksX; ++blockX) {
            const int uniqueBlock = blockToUnique[static_cast<size_t>(blockY) * static_cast<size_t>(importBlocksX) + static_cast<size_t>(blockX)];
            const uint16_t block = uniqueBlock < 0 ? 0 : static_cast<uint16_t>(targetBlock + static_cast<unsigned>(uniqueBlock));
            SetScratchLevelBlock(core, blockX, blockY, block);
        }
    }

    state.selectedBlock = static_cast<uint16_t>(targetBlock);
    state.levelRenderer.Invalidate();
    const std::string paletteLog = mode7
        ? "Mode 7 palette 0"
        : "palettes " + ScratchPaletteList(targetPalettes, palettePlan.paletteCount);
    const std::string tileLog = requestedTile == targetTile
        ? "tile " + std::to_string(targetTile)
        : "tile " + std::to_string(targetTile) + " (moved from " + std::to_string(requestedTile) + " to avoid foreground)";
    AddLog("Scratch Board imported " + target + " PNG: " + std::to_string(uniqueTiles.size()) + " tile(s) at " + tileLog + ", " + std::to_string(uniqueBlocks.size()) + " block(s), " + paletteLog + ".");
    state.session.LoadCurrentLayer(state.showBackground);
    state.levelRenderer.Invalidate();
    return true;
}

static bool ImportScratchImageAsBackground(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        AddLog("Scratch Board background import failed: load a ROM first.");
        return false;
    }

    const bool previousBackground = state.showBackground;
    if (!state.session.LoadCurrentLayer(false)) {
        AddLog("Scratch Board background import failed: could not scan foreground layer.");
        return false;
    }
    const std::vector<bool> foregroundUsedTiles = CollectScratchForegroundUsedTiles(state);

    if (!state.session.LoadCurrentLayer(true)) {
        AddLog("Scratch Board background import failed: could not load background layer.");
        if (previousBackground) {
            state.showBackground = true;
        }
        return false;
    }

    state.showBackground = true;
    const bool imported = ImportScratchImageAsLevel(
        state, "background", ScratchSelectedImportPalettes(), &foregroundUsedTiles, true);

    if (!imported && !previousBackground) {
        state.session.LoadCurrentLayer(false);
        state.showBackground = false;
    }

    state.levelRenderer.Invalidate();
    return imported;
}

static bool ImportScratchImageAsForeground(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        AddLog("Scratch Board level import failed: load a ROM first.");
        return false;
    }

    if (!state.session.LoadCurrentLayer(false)) {
        AddLog("Scratch Board level import failed: could not load foreground layer.");
        return false;
    }

    state.showBackground = false;
    const bool imported = ImportScratchImageAsLevel(
        state, "level", ScratchSelectedImportPalettes(), nullptr, false, true);
    state.levelRenderer.Invalidate();
    return imported;
}

static void DrawScratchBoard(EditorState& state, ID3D11Device* device, const std::vector<std::wstring>& droppedFiles)
{
    ProcessScratchDrops(device, droppedFiles);

    if (ImGui::Button("Clear")) {
        for (ScratchImage& image : g_scratchImages) {
            ReleaseScratchImage(image);
        }
        g_scratchImages.clear();
        g_selectedScratchImage = -1;
    }
    ImGui::SameLine();
    ImGui::Text("%d image%s", static_cast<int>(g_scratchImages.size()), g_scratchImages.size() == 1 ? "" : "s");

    if (g_selectedScratchImage >= 0 && g_selectedScratchImage < static_cast<int>(g_scratchImages.size())) {
        ImGui::SameLine();
        if (ImGui::Button("Convert to Tileset")) {
            ConvertScratchImageToTileset(device, g_selectedScratchImage);
        }
        if (g_selectedScratchImage >= 0 && g_selectedScratchImage < static_cast<int>(g_scratchImages.size())) {
            ScratchImage& selected = g_scratchImages[g_selectedScratchImage];
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderFloat("Zoom", &selected.scale, 0.1f, 8.0f, "%.2fx");
        }
    }
    if (g_selectedScratchImage >= 0 && g_selectedScratchImage < static_cast<int>(g_scratchImages.size())) {
        ImGui::TextUnformatted("Target palettes");
        for (int palette = 0; palette < static_cast<int>(g_scratchImportPalettes.size()); ++palette) {
            if (palette > 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(palette);
            const std::string label = std::to_string(palette);
            ImGui::Checkbox(label.c_str(), &g_scratchImportPalettes[palette]);
            ImGui::PopID();
        }

        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Target tile", &g_scratchTileTarget, 1, 10, ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SameLine();
        if (ImGui::Button("Import Level PNG")) {
            ImportScratchImageAsForeground(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Background PNG")) {
            ImportScratchImageAsBackground(state);
        }
    }

    ImGui::BeginChild("scratch-board-canvas", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(12, 13, 15, 255));

    if (g_scratchImages.empty()) {
        ImGui::TextDisabled("Drag PNG files onto the editor window.");
    }

    for (int i = 0; i < static_cast<int>(g_scratchImages.size()); ++i) {
        ScratchImage& image = g_scratchImages[i];
        if (!image.texture) {
            continue;
        }

        const ImVec2 pos(origin.x + image.pos.x, origin.y + image.pos.y);
        const ImVec2 size(static_cast<float>(image.width) * image.scale, static_cast<float>(image.height) * image.scale);
        const ImVec2 bottomRight(pos.x + size.x, pos.y + size.y);
        drawList->AddImage(reinterpret_cast<ImTextureID>(image.texture), pos, bottomRight);
        drawList->AddRect(pos, bottomRight, i == g_selectedScratchImage ? IM_COL32(255, 235, 120, 255) : IM_COL32(90, 105, 112, 190), 0.0f, 0, i == g_selectedScratchImage ? 2.0f : 1.0f);
        if (image.uniqueColorCount < 0) {
            image.uniqueColorCount = CountScratchUniqueColors(image);
        }
        const std::string label = image.name + " - " + std::to_string(image.uniqueColorCount) + " colors";
        drawList->AddText(ImVec2(pos.x, bottomRight.y + 3.0f), IM_COL32(220, 225, 225, 255), label.c_str());

        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(i);
        ImGui::InvisibleButton("scratch-image", size);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 localPixel(
            std::clamp((mouse.x - pos.x) / image.scale, 0.0f, static_cast<float>(image.width)),
            std::clamp((mouse.y - pos.y) / image.scale, 0.0f, static_cast<float>(image.height)));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            g_selectedScratchImage = i;
            if (ImGui::GetIO().KeyShift) {
                g_scratchSelectingPixels = true;
                g_scratchSelectStart = localPixel;
                g_scratchSelectEnd = localPixel;
            }
        }
        if (ImGui::IsItemActive() && g_scratchSelectingPixels && g_selectedScratchImage == i && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            g_scratchSelectEnd = localPixel;
        } else if (ImGui::IsItemActive() && !g_scratchSelectingPixels && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            image.pos.x += delta.x;
            image.pos.y += delta.y;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_scratchSelectingPixels = false;
        }
        if (g_selectedScratchImage == i) {
            int left = static_cast<int>(std::floor((std::min)(g_scratchSelectStart.x, g_scratchSelectEnd.x)));
            int right = static_cast<int>(std::ceil((std::max)(g_scratchSelectStart.x, g_scratchSelectEnd.x)));
            int top = static_cast<int>(std::floor((std::min)(g_scratchSelectStart.y, g_scratchSelectEnd.y)));
            int bottom = static_cast<int>(std::ceil((std::max)(g_scratchSelectStart.y, g_scratchSelectEnd.y)));
            left = std::clamp(left, 0, image.width);
            right = std::clamp(right, 0, image.width);
            top = std::clamp(top, 0, image.height);
            bottom = std::clamp(bottom, 0, image.height);
            if (right > left && bottom > top) {
                const ImVec2 selMin(pos.x + left * image.scale, pos.y + top * image.scale);
                const ImVec2 selMax(pos.x + right * image.scale, pos.y + bottom * image.scale);
                drawList->AddRectFilled(selMin, selMax, IM_COL32(255, 235, 120, 35));
                drawList->AddRect(selMin, selMax, IM_COL32(255, 235, 120, 255), 0.0f, 0, 2.0f);
            }
        }
        if (ImGui::BeginPopupContextItem("scratch-context")) {
            if (ImGui::MenuItem("Remove")) {
                ReleaseScratchImage(image);
                g_scratchImages.erase(g_scratchImages.begin() + i);
                if (g_selectedScratchImage >= static_cast<int>(g_scratchImages.size())) {
                    g_selectedScratchImage = static_cast<int>(g_scratchImages.size()) - 1;
                }
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopID();
                break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
}

struct EventPaletteTemplate {
    const char* name;
    BYTE type;
    WORD id;
    WORD subId;
    WORD unknown;
};

static const std::vector<EventPaletteTemplate>& EventPaletteTemplates()
{
    static const std::vector<EventPaletteTemplate> templates = {
        { "Projectile", EVENT_TYPE_ENEMY, 0x01, 0, 3 },
        { "Bone", EVENT_TYPE_ENEMY, 0x02, 0, 3 },
        { "Ring", EVENT_TYPE_ENEMY, 0x03, 0, 3 },
        { "Platform", EVENT_TYPE_ENEMY, 0x06, 0, 3 },
        { "Medusa Head", EVENT_TYPE_ENEMY, 0x07, 0, 3 },
        { "Ghost", EVENT_TYPE_ENEMY, 0x08, 0, 3 },
        { "Porcupine", EVENT_TYPE_ENEMY, 0x09, 0, 3 },
        { "Dog", EVENT_TYPE_ENEMY, 0x0A, 0, 3 },
        { "Bone Pillar", EVENT_TYPE_ENEMY, 0x0B, 0, 3 },
        { "Bat", EVENT_TYPE_ENEMY, 0x0C, 0, 3 },
        { "Secret Man", EVENT_TYPE_ENEMY, 0x0D, 0, 3 },
        { "Candle Main", EVENT_TYPE_ENEMY, 0x0E, 0, 3 },
        { "Book Bird", EVENT_TYPE_ENEMY, 0x0F, 0, 3 },
        { "Bird", EVENT_TYPE_ENEMY, 0x10, 0, 3 }, 
        { "Skeleton", EVENT_TYPE_ENEMY, 0x11, 0, 3 },
        { "Skeleton Bone", EVENT_TYPE_ENEMY, 0x12, 0, 3 }, 
        { "Crusher", EVENT_TYPE_ENEMY, 0x16, 0, 3 },
        { "Moving Platform", EVENT_TYPE_ENEMY, 0x17, 0, 3 },    
        
        { "Heart", EVENT_TYPE_CANDLE, 0x18, 0, 3 },
        { "Big Heart", EVENT_TYPE_CANDLE, 0x19, 0, 3 },
        { "Knife", EVENT_TYPE_CANDLE, 0x1A, 0, 3 },
        { "Axe", EVENT_TYPE_CANDLE, 0x1B, 0, 3 },
        { "Holy Water", EVENT_TYPE_CANDLE, 0x1C, 0, 3 },
        { "Cross", EVENT_TYPE_CANDLE, 0x1D, 0, 3 },
        { "Stopwatch", EVENT_TYPE_CANDLE, 0x1E, 0, 3 },
        { "Rosary", EVENT_TYPE_CANDLE, 0x1F, 0, 3 },
        { "Potion", EVENT_TYPE_CANDLE, 0x20, 0, 3 },
        { "Whip Upgrade", EVENT_TYPE_CANDLE, 0x21, 0, 3 },
        { "Money 100", EVENT_TYPE_CANDLE, 0x22, 0, 3 },
        { "Double", EVENT_TYPE_CANDLE, 0x23, 0, 3 },
        { "Triple", EVENT_TYPE_CANDLE, 0x24, 0, 3 },
        { "Small Meat", EVENT_TYPE_CANDLE, 0x25, 0, 3 },
        { "Large Meat", EVENT_TYPE_CANDLE, 0x26, 0, 3 },
        { "Orb", EVENT_TYPE_CANDLE, 0x27, 0, 3 },
        { "1Up", EVENT_TYPE_CANDLE, 0x28, 0, 3 },
        { "Money 300", EVENT_TYPE_CANDLE, 0x62, 0, 3 },
        { "Money 500", EVENT_TYPE_CANDLE, 0xA2, 0, 3 },
        { "Money 700", EVENT_TYPE_CANDLE, 0xE2, 0, 3 },
        
        { "Wall Corpse", EVENT_TYPE_ENEMY, 0x2C, 0, 3 },
        { "Moon", EVENT_TYPE_ENEMY, 0x2E, 0, 3 },
        { "Frog", EVENT_TYPE_ENEMY, 0x30, 0, 3 },             
        { "Sword Skeleton", EVENT_TYPE_ENEMY, 0x31, 0, 3 }, 
        { "Hanging Snakes", EVENT_TYPE_ENEMY, 0x32, 0, 3 },
        { "Coffin Sniper", EVENT_TYPE_ENEMY, 0x33, 0, 3 }, 
        { "Mud Man", EVENT_TYPE_ENEMY, 0x34, 0, 3 },
        { "Plant", EVENT_TYPE_ENEMY, 0x35, 0, 3 }, 
        { "High Five Skelly", EVENT_TYPE_ENEMY, 0x36, 0, 3 },
        { "Crumbling Block", EVENT_TYPE_ENEMY, 0x37, 0, 3 },
        { "Falling Pillar", EVENT_TYPE_ENEMY, 0x39, 0, 3 },
        { "Sinking Bridge", EVENT_TYPE_ENEMY, 0x3A, 0, 3 },
        { "Turning Platform", EVENT_TYPE_ENEMY, 0x3B, 0, 3 },
        { "Leaf Monster", EVENT_TYPE_ENEMY, 0x3C, 0, 3 }, 
        { "Big Flame", EVENT_TYPE_ENEMY, 0x3D, 0, 3 },
        { "Gargoyle", EVENT_TYPE_ENEMY, 0x3E, 0, 3 }, 
        { "Drip", EVENT_TYPE_ENEMY, 0x3F, 0, 3 }, 
        { "Unused Turning Platform", EVENT_TYPE_ENEMY, 0x40, 0, 3 },
        { "Table", EVENT_TYPE_ENEMY, 0x42, 0, 3 },
        { "Spider", EVENT_TYPE_ENEMY, 0x43, 0, 3 },
        { "Stalactite", EVENT_TYPE_ENEMY, 0x44, 0, 3 },
        { "Platform Spikes", EVENT_TYPE_ENEMY, 0x4A, 0, 3 },
        { "Unused Bat", EVENT_TYPE_ENEMY, 0x4B, 0, 3 }, 
        { "Fish Man Swim", EVENT_TYPE_ENEMY, 0x4C, 0, 3 },
        { "Chandelier", EVENT_TYPE_ENEMY, 0x4D, 0, 3 },
        { "Diving Bat", EVENT_TYPE_ENEMY, 0x4E, 0, 3 }, 
        { "Unknown", EVENT_TYPE_ENEMY, 0x4F, 0, 3 },
        { "Fish Man Jump", EVENT_TYPE_ENEMY, 0x51, 0, 3 }, 
        { "Axe Knight", EVENT_TYPE_ENEMY, 0x52, 0, 3 },
        { "Axe Projectile", EVENT_TYPE_ENEMY, 0x53, 0, 3 },
        { "Zombie Ghost", EVENT_TYPE_ENEMY, 0x54, 0, 3 },
        { "Whip Skeleton", EVENT_TYPE_ENEMY, 0x56, 0, 3 }, 
        { "Hunchback", EVENT_TYPE_ENEMY, 0x57, 0, 3 },
        { "Harpie", EVENT_TYPE_ENEMY, 0x58, 0, 3 }, 
        { "Spear Knight", EVENT_TYPE_ENEMY, 0x59, 0, 3 },
        { "Woman Ghost", EVENT_TYPE_ENEMY, 0x5A, 0, 3 }, 
        { "Ghost Man", EVENT_TYPE_ENEMY, 0x5B, 0, 3 },
        { "Sword Hands", EVENT_TYPE_ENEMY, 0x5C, 0, 3 }, 
        { "Bone Dragon", EVENT_TYPE_ENEMY, 0x5D, 0, 3 },
        { "Bone Dragon 2", EVENT_TYPE_ENEMY, 0x5E, 0, 3 }, 
        { "Falling Dagger", EVENT_TYPE_ENEMY, 0x60, 0, 3 },
        { "Spike Platform", EVENT_TYPE_ENEMY, 0x61, 0, 3 },
        { "Moving Spikes", EVENT_TYPE_ENEMY, 0x62, 0, 3 },
        { "Suck Hole", EVENT_TYPE_ENEMY, 0x63, 0, 3 },
        { "Secret Cave Hole", EVENT_TYPE_ENEMY, 0x64, 0, 3 },
        { "Grave Hand", EVENT_TYPE_ENEMY, 0x66, 0, 3 }, 
        { "Watching Skulls", EVENT_TYPE_ENEMY, 0x68, 0, 3 },
        { "Red Skeleton", EVENT_TYPE_ENEMY, 0x69, 0, 3 },
        { "Candle Dog", EVENT_TYPE_ENEMY, 0x6B, 0, 3 },
        { "Ceiling Skelly", EVENT_TYPE_ENEMY, 0x6C, 0, 3 },
        { "Fuzzy Ball", EVENT_TYPE_ENEMY, 0x6D, 0, 3 }, 
        { "Stealing Hand", EVENT_TYPE_ENEMY, 0x6E, 0, 3 },
        { "Horse Head Upside Down", EVENT_TYPE_ENEMY, 0x6F, 0, 3 },
        { "Grave Digger", EVENT_TYPE_ENEMY, 0x70, 0, 3 }, 
        { "Horse Head", EVENT_TYPE_ENEMY, 0x71, 0, 3 },
        { "Eye", EVENT_TYPE_ENEMY, 0x72, 0, 3 }, 
        { "Club Guy", EVENT_TYPE_ENEMY, 0x73, 0, 3 },
        { "Caterpillar", EVENT_TYPE_ENEMY, 0x74, 0, 3 }, 
        { "Shield Gargoyle", EVENT_TYPE_ENEMY, 0x75, 0, 3 },
        { "Dancing Couple", EVENT_TYPE_ENEMY, 0x76, 0, 3 }, 
        { "Mudman Small", EVENT_TYPE_ENEMY, 0x78, 0, 3 },
        { "Mudman Tinny", EVENT_TYPE_ENEMY, 0x79, 0, 3 },
        { "Carpet Monster", EVENT_TYPE_ENEMY, 0x7A, 0, 3 },
        { "Coffin Circle", EVENT_TYPE_ENEMY, 0x7B, 0, 3 }, 
        { "Gear", EVENT_TYPE_ENEMY, 0x7C, 0, 3 },
        { "Headless Knight", EVENT_TYPE_ENEMY, 0x7E, 0, 3 },
        { "Rock Man", EVENT_TYPE_ENEMY, 0x7F, 0, 3 },
        
        { "PullBridge", EVENT_TYPE_OBJECT, 0x04, 0, 3 },
        { "SwitchBG", EVENT_TYPE_OBJECT, 0x05, 0, 3 },
        { "Pillar Exit", EVENT_TYPE_OBJECT, 0x14, 0, 3 }, 
        { "Exit", EVENT_TYPE_OBJECT, 0x15, 0, 3 },
        { "Boss Load", EVENT_TYPE_OBJECT, 0x2A, 0, 3 },
        { "Iron Gate", EVENT_TYPE_OBJECT, 0x2B, 0, 3 },
        { "Small Flame", EVENT_TYPE_OBJECT, 0x2D, 0, 3 },        
        { "Breakable Block", EVENT_TYPE_OBJECT, 0x2F, 0, 3 }, 
        { "Auto Spawner", EVENT_TYPE_OBJECT, 0x38, 0, 3 },     
        { "Big Flame", EVENT_TYPE_OBJECT, 0x3D, 0, 3 },
        { "Cam Lock", EVENT_TYPE_OBJECT, 0x41, 0, 3 },
        { "Unknown Falling", EVENT_TYPE_OBJECT, 0x45, 0, 3 },
        { "Breakable Stairs", EVENT_TYPE_OBJECT, 0x46, 0, 3 },
        { "Unknown", EVENT_TYPE_OBJECT, 0x47, 0, 3 },
        { "Special Loader", EVENT_TYPE_OBJECT, 0x48, 0, 3 },
        { "Vains on Fance", EVENT_TYPE_OBJECT, 0x49, 0, 3 },
        { "Unknown", EVENT_TYPE_OBJECT, 0x4F, 0, 3 },
        { "Splash Unknown", EVENT_TYPE_OBJECT, 0x50, 0, 3 }, 
        { "Bridge Robe", EVENT_TYPE_OBJECT, 0x55, 0, 3 },
        { "Ectoplasm", EVENT_TYPE_OBJECT, 0x5F, 0, 3 },
        { "Gold Platform Splash", EVENT_TYPE_OBJECT, 0x63, 0, 3 },  
        { "Falling Blocks", EVENT_TYPE_OBJECT, 0x65, 0, 3 },
        { "Place Holder", EVENT_TYPE_OBJECT, 0x67, 0, 3 },
        { "Falling Debris", EVENT_TYPE_OBJECT, 0x6A, 0, 3 },
        { "Stage B", EVENT_TYPE_OBJECT, 0x7D, 0, 3 },
    };
    return templates;
}

static int EventPaletteCategory(const EventPaletteTemplate& item)
{
    if (item.type == EVENT_TYPE_CANDLE) {
        return 0;
    }
    if (item.type == EVENT_TYPE_OBJECT) {
        return 2;
    }
    return 1;   
}

static void DrawTools(EditorState& state, HWND hwnd, ID3D11Device* device, const std::vector<std::wstring>& droppedFiles)
{
    std::string musicLog;
    std::string instrumentLog;
    ImGui::Begin("Tools");
    if (ImGui::BeginTabBar("tool-tabs")) {
        const int restoredToolTab = state.activeToolTab;
        const auto toolFlags = [&](int index) {
            return state.restoreToolTab && restoredToolTab == index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabItem("Edit Events", nullptr, toolFlags(0))) {
            if (!state.restoreToolTab || restoredToolTab == 0) {
                state.activeToolTab = 0;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            SC4Core& core = state.session.Core();
            if (ImGui::BeginTabBar("event-palette-tabs")) {
                static const char* tabNames[] = { "Candles", "Enemies", "Misc" };
                const int restoredEventTab = state.activeEventPaletteTab;
                for (int tab = 0; tab < 3; ++tab) {
                    const ImGuiTabItemFlags flags = state.restoreEventPaletteTab && restoredEventTab == tab
                        ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                    if (!ImGui::BeginTabItem(tabNames[tab], nullptr, flags)) {
                        continue;
                    }
                    if (!state.restoreEventPaletteTab || restoredEventTab == tab) {
                        state.activeEventPaletteTab = tab;
                        state.restoreEventPaletteTab = false;
                    }

                    ImGui::BeginChild(tabNames[tab], ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    const float cellSize = 56.0f;
                    const float spacing = ImGui::GetStyle().ItemSpacing.x;
                    const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cellSize + spacing)));
                    int column = 0;

                    for (const EventPaletteTemplate& item : EventPaletteTemplates()) {
                        if (EventPaletteCategory(item) != tab) {
                            continue;
                        }

                        EventInfo event = {};
                        event.type = item.type;
                        event.eventId = item.id;
                        event.eventSubId = item.subId;
                        event.unknown = item.unknown;
                        const EventDragPayload payload = { item.type, item.id, item.subId, item.unknown };

                        ImGui::PushID((static_cast<int>(item.type) << 16) | item.id);
                        ImGui::InvisibleButton("##event-cell", ImVec2(cellSize, cellSize));
                        const ImVec2 itemMin = ImGui::GetItemRectMin();
                        const ImVec2 itemMax = ImGui::GetItemRectMax();
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(itemMin, itemMax, IM_COL32(24, 24, 28, 255));
                        state.levelRenderer.DrawEventThumbnail(core, drawList, ImVec2(itemMin.x + 4.0f, itemMin.y + 4.0f), ImVec2(itemMax.x - 4.0f, itemMax.y - 4.0f), event);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\nType %u  ID %u.%u", item.name, item.type, item.id & 0xFF, item.subId & 0xFF);
                        }
                        if (ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload(kSc4EventDragPayloadType, &payload, sizeof(payload));
                            ImGui::Text("%s", item.name);
                            ImGui::Text("Type %u  ID %u.%u", item.type, item.id & 0xFF, item.subId & 0xFF);
                            ImGui::EndDragDropSource();
                        }
                        ImGui::PopID();

                        ++column;
                        if (column < columns) {
                            ImGui::SameLine();
                        } else {
                            column = 0;
                        }
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Draw Tiles", nullptr, toolFlags(1))) {
            if (!state.restoreToolTab || restoredToolTab == 1) {
                state.activeToolTab = 1;
                state.restoreToolTab = false;
            }
            state.editLevelMode = true;
            int clipboardPalette = static_cast<int>(state.drawTilesClipboardPalette & 0x7);
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::InputInt("Clipboard palette", &clipboardPalette)) {
                state.drawTilesClipboardPalette = static_cast<unsigned>(std::clamp(clipboardPalette, 0, 7));
            }
            const unsigned palette = state.drawTilesClipboardPalette & 0x7;
            ImGui::SameLine();
            if (ImGui::Button("Copy Tiles")) {
                AddLog(state.levelRenderer.CopyAvailableTilesToClipboard(hwnd, state.session, palette)
                    ? "Copied available tiles to clipboard."
                    : "Copy tiles failed.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Paste Tiles")) {
                RomUndoSnapshot beforePaste = state.session.CreateUndoSnapshot(state.selectedEventIndex);
                if (state.levelRenderer.PasteClipboardToAvailableTiles(hwnd, state.session, palette)) {
                    CommitUndoSnapshot(state, std::move(beforePaste));
                    AddLog("Pasted clipboard image into available tiles.");
                } else {
                    AddLog("Paste tiles failed.");
                }
            }
            ImGui::Separator();
            if (state.levelRenderer.DrawBlockPalette(state.session, &state.selectedBlock)) {
                state.blockBrushWidth = 1;
                state.blockBrushHeight = 1;
                state.blockBrush.assign(1, state.selectedBlock);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Edit Blocks", nullptr, toolFlags(2))) {
            if (!state.restoreToolTab || restoredToolTab == 2) {
                state.activeToolTab = 2;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            state.levelRenderer.DrawBlockEditor(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tile Behavior", nullptr, toolFlags(3))) {
            if (!state.restoreToolTab || restoredToolTab == 3) {
                state.activeToolTab = 3;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            state.levelRenderer.DrawTileBehaviorEditor(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HUD", nullptr, toolFlags(4))) {
            if (!state.restoreToolTab || restoredToolTab == 4) {
                state.activeToolTab = 4;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            DrawHudEditor(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Edit Sprites", nullptr, toolFlags(5))) {
            if (!state.restoreToolTab || restoredToolTab == 5) {
                state.activeToolTab = 5;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            DrawSpriteEditor(state, hwnd);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scratch Board", nullptr, toolFlags(6))) {
            if (!state.restoreToolTab || restoredToolTab == 6) {
                state.activeToolTab = 6;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            DrawScratchBoard(state, device, droppedFiles);
            ImGui::EndTabItem();
        } else {
            ProcessScratchDrops(device, droppedFiles);
        }
        if (ImGui::BeginTabItem("Edit Music", nullptr, toolFlags(7))) {
            if (!state.restoreToolTab || restoredToolTab == 7) {
                state.activeToolTab = 7;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            DrawMusicEditor(state, hwnd, musicLog);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Edit Instruments", nullptr, toolFlags(8))) {
            if (!state.restoreToolTab || restoredToolTab == 8) {
                state.activeToolTab = 8;
                state.restoreToolTab = false;
            }
            state.editLevelMode = false;
            DrawInstrumentEditor(state, hwnd, instrumentLog);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!musicLog.empty()) AddLog(musicLog);
    if (!instrumentLog.empty()) AddLog(instrumentLog);
    ImGui::End();
}

static void HelpRow(const char* label, const char* text)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", text);
}

static void HelpBullets(std::initializer_list<const char*> items)
{
    for (const char* item : items) {
        ImGui::BulletText("%s", item);
    }
}

static void DrawHelpView(EditorState& state)
{
    if (!state.showHelp) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Help###HelpView", &state.showHelp)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("SC4Ed ImGui Help");
    ImGui::TextWrapped("This view explains the main editor windows, common workflows, mouse actions, and keyboard shortcuts.");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Getting Started", ImGuiTreeNodeFlags_DefaultOpen)) {
        HelpBullets({
            "Use File > Open ROM... to load a Super Castlevania IV ROM.",
            "Use Navigator to choose the level and checkpoint, then inspect the result in Level View.",
            "Most editing tools are in Tools. Choose a tab there, then interact with Level View or the relevant tile/sprite preview.",
            "The Selection tab gives you extra options when you click on events like. Exits, CamLock, breakable walls etc.",
            "Use File > Save or Ctrl+S after editing. Save As writes the current ROM to a different path.",
            "Use View > Reset Default Layout if panels are missing or docked somewhere awkward."
        });
    }

    if (ImGui::CollapsingHeader("Keyboard Shortcuts", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("help-shortcuts", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Action");
            ImGui::TableHeadersRow();
            HelpRow("Ctrl+S", "Save the current ROM.");
            HelpRow("Ctrl+Z", "Undo the last supported ROM edit.");
            HelpRow("Ctrl+Y / Ctrl+Shift+Z", "Redo the last undone ROM edit.");
            HelpRow("Delete", "Delete the selected event in Level View.");
            HelpRow("Mouse wheel", "Scroll panels and lists. In Level View, use the Zoom slider for level scale.");
            HelpRow("Middle drag", "Pan the Level View.");
            HelpRow("Left click", "Select or paint, depending on the active tool.");
            HelpRow("Right click", "Sample or copy in level/block editing, and copy-drag events.");
            HelpRow("Drag/drop", "Drag event templates from Tools > Edit Events into Level View. Drop PNG files onto Scratch Board.");
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Classic Win32 emulator shortcuts such as F12 and save-state keys are not currently part of the ImGui port.");
    }

    if (ImGui::CollapsingHeader("Views and Panels", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("help-views", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("Purpose");
            ImGui::TableHeadersRow();
            HelpRow("Level View", "Main rendered level canvas. Use it to inspect levels, select and drag events, paint blocks, and preview collision/events/grid overlays.");
            HelpRow("ROM", "Shows the loaded file path, ROM size, header status, checksum, and current level dimensions.");
            HelpRow("Navigator", "Changes level, checkpoint, zoom, selected paint block, and visibility overlays.");
            HelpRow("Palette", "Shows all 16 palettes. Click a swatch to select it, then use the color picker to write color changes back to the ROM.");
            HelpRow("Tools", "Holds the editing tabs: events, tiles, blocks, behavior, HUD, sprites, and scratch image import/conversion.");
            HelpRow("Global Properties", "ROM-wide properties and values exposed by the property editor.");
            HelpRow("Level Properties", "Current-level properties exposed by the property editor.");
            HelpRow("Selection", "Details and editable fields for the currently selected event or object.");
            HelpRow("Log", "Recent status messages such as loaded ROMs, saves, failed operations, and import results.");
            HelpRow("Help", "This reference view. Toggle it from View > Help or Help > Help View.");
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Level View Mouse Actions")) {
        HelpBullets({
            "Middle-click and drag pans the level.",
            "When Tools > Draw Tiles is active, left-drag paints the selected block into the level.",
            "In Draw Tiles mode, right-click samples a block. Right-drag selects a rectangular block brush from the level.",
            "When events are visible, left-click an event to select it and left-drag to move it.",
            "Right-drag an event to clone/copy it while dragging.",
            "Drag an event template from Tools > Edit Events and drop it onto Level View to create a new event."
        });
    }

    if (ImGui::CollapsingHeader("Tool Tabs")) {
        if (ImGui::BeginTable("help-tools", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Tab", ImGuiTableColumnFlags_WidthFixed, 135.0f);
            ImGui::TableSetupColumn("What it does");
            ImGui::TableHeadersRow();
            HelpRow("Edit Events", "Event template palette. Browse Candles, Enemies, and Misc, then drag templates into Level View.");
            HelpRow("Draw Tiles", "Block painting mode. Select a block from the block palette, paint it into Level View, and copy/paste available tiles through the clipboard.");
            HelpRow("Edit Blocks", "Edit the 4x4 tile makeup of a block. Pick a block, choose tiles/palette/flip flags, and paint individual block cells.");
            HelpRow("Tile Behavior", "Inspect and edit tile behavior types. Expanded ROMs can write per-tile behavior directly.");
            HelpRow("HUD", "Edit HUD tile items and positions with a preview canvas and item list.");
            HelpRow("Edit Sprites", "Edit sprite/tile graphics and palettes for global or level-specific sprite data.");
            HelpRow("Scratch Board", "Drop or import PNG artwork, choose target palettes and a starting tile, or import image data as level/background art.");
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Buttons and Controls")) {
        if (ImGui::BeginTable("help-controls", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 155.0f);
            ImGui::TableSetupColumn("Explanation");
            ImGui::TableHeadersRow();
            HelpRow("Open ROM...", "Loads a SNES ROM file into the editor.");
            HelpRow("Save / Save As...", "Writes current ROM edits to disk. Save As chooses a new output path.");
            HelpRow("Export BPS Patch...", "Creates a BPS patch from an original unmodified ROM and the current edited ROM in memory.");
            HelpRow("ROM Expander...", "Expands the loaded ROM so editors that need extra editable tables can write their data.");
            HelpRow("Show Background", "Switches the rendered level layer between foreground and background where supported.");
            HelpRow("Show Collision", "Draws collision information on top of the foreground view.");
            HelpRow("Show Events", "Shows event markers and enables event selection/dragging.");
            HelpRow("Show Grid", "Draws block grid lines over the level.");
            HelpRow("Level / Checkpoint", "Loads a different level or checkpoint.");
            HelpRow("Zoom", "Changes the Level View scale without altering ROM data.");
            HelpRow("Paint block", "Manually sets the block ID used by Draw Tiles painting.");
            HelpRow("Copy Tiles", "Copies available tiles for the current palette to the clipboard as an image.");
            HelpRow("Paste Tiles", "Pastes clipboard image data into available tiles for the current palette.");
            HelpRow("Clear", "Clears Scratch Board images.");
            HelpRow("Convert to Tileset", "Analyzes the selected Scratch Board image into tile-style source material.");
            HelpRow("Target palettes", "Selects which of palettes 0-7 foreground and background PNG imports may use.");
            HelpRow("Target tile", "Sets the first writable ROM tile used by foreground and background PNG imports.");
            HelpRow("Import Level PNG", "Imports the selected Scratch Board image as foreground level art.");
            HelpRow("Import Background PNG", "Imports the selected Scratch Board image as background art.");
            HelpRow("Sort Events", "Sorts event data in the current level.");
           // HelpRow("Slot Events", "Assigns/rebuilds event slots for the current event data.");
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Editing Notes")) {
        HelpBullets({
            "Palette edits write through mirrored source offsets when the color is reused by the ROM scripts.",
            "Some properties and behavior edits depend on whether the ROM has been expanded.",
            "The undo stack covers supported ROM mutations in the ImGui port; save often when exploring unknown data.",
            "If a panel seems gone, use View > Help to reopen this view or View > Reset Default Layout to restore the whole workspace."
        });
    }

    ImGui::End();
}

static void DrawLog()
{
    ImGui::Begin("Log");
    ImGui::BeginChild("log-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (const std::string& line : g_logMessages) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

static void DrawProperties(EditorState& state)
{
    ImGui::Begin("Global Properties");
    DrawGlobalPropertiesTab(state);
    ImGui::End();

    ImGui::Begin("Level Properties");
    DrawLevelPropertiesTab(state);
    ImGui::End();

    ImGui::Begin("Selection");
    DrawSelectionTab(state);
    ImGui::End();
}

static void DrawDockSpace(EditorState& state, HWND hwnd)
{
    static bool dockspaceOpen = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("SC4Ed DockSpace", &dockspaceOpen, flags);
    ImGui::PopStyleVar(2);

    DrawTopBar(state, hwnd);
    const ImGuiID dockspaceId = ImGui::GetID("MainDockSpaceV4");
    ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
    const bool emptyDockspace = dockNode == nullptr || (!dockNode->IsSplitNode() && dockNode->Windows.Size == 0);
    if (g_resetDefaultDockLayout || emptyDockspace) {
        BuildDefaultDockLayout(dockspaceId, ImGui::GetContentRegionAvail());
        g_resetDefaultDockLayout = false;
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void DrawEditorUi(EditorState& state, HWND hwnd, ID3D11Device* device, const std::vector<std::wstring>& droppedFiles)
{
    hWID[0] = hwnd;
    std::string windowTitle = "SC4Ed ImGui";
    if (state.session.IsLoaded()) {
        const std::string& path = state.session.Info().path;
        const size_t slash = path.find_last_of("\\/");
        windowTitle += " - " + path.substr(slash == std::string::npos ? 0 : slash + 1);
        if (state.session.IsDirty()) {
            windowTitle += " *";
        }
    }
    static std::string previousWindowTitle;
    if (windowTitle != previousWindowTitle) {
        SetWindowTextA(hwnd, windowTitle.c_str());
        previousWindowTitle = windowTitle;
    }

    const bool emulatorUsesKeyboard = state.internalEmulatorRunning
        && Emulator::Instance()->GetState() != Emulator::EmuState::OFF
        && g_internalEmulatorCapturesKeyboard;
    if (emulatorUsesKeyboard) {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    } else {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    const bool editorShortcut = state.session.IsLoaded()
        && !emulatorUsesKeyboard
        && ImGui::GetIO().KeyCtrl
        && !ImGui::GetIO().WantTextInput;
    if (editorShortcut && ImGui::IsKeyPressed(ImGuiKey_S)) {
        AddLog(state.session.Save() ? SaveLogMessage(state.session.Info().path) : "Save failed: " + state.session.LastError());
    }
    if (editorShortcut && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (ImGui::GetIO().KeyShift) {
            PerformRedo(state);
        } else {
            PerformUndo(state);
        }
    } else if (editorShortcut && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        PerformRedo(state);
    }

    if (state.session.IsLoaded()
        && !emulatorUsesKeyboard
        && state.selectedEventIndex >= 0
        && !ImGui::GetIO().WantTextInput
        && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        PushUndo(state);
        if (state.session.DeleteEvent(state.selectedEventIndex)) {
            state.levelRenderer.Invalidate();
        }
    }

    DrawDockSpace(state, hwnd);
    g_internalEmulatorCapturesKeyboard = false;
    DrawPalettePanel(state);
    DrawSidebar(state);
    DrawViewport(state, device);
    DrawInternalEmulator(state, device);
    DrawTools(state, hwnd, device, droppedFiles);
    DrawLog();
    DrawHelpView(state);
    DrawProperties(state);
}
