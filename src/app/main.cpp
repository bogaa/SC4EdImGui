#include "D3D11Host.h"
#include "EditorState.h"
#include "Ui.h"
#include "Emulator.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <algorithm>
#include <string>
#include <vector>

#include "resource.h"   // windows, resource.h, SC4EdImGui.rc, added to make list and replaced WNDCLASSEXW

static D3D11Host g_d3d;
static std::vector<std::wstring> g_droppedFiles;
static EditorState* g_editorState = nullptr;
static std::wstring g_iniPath;
static std::string g_iniPathUtf8;
static bool g_uiSettingsCaptured = false;

struct SavedWindowPlacement {
    int x = 100;
    int y = 100;
    int width = 1440;
    int height = 900;
    bool maximized = true;
};

static SavedWindowPlacement g_exitWindowPlacement;
static int g_exitToolTab = 0;
static int g_exitEventPaletteTab = 0;
static int g_exitSpriteTab = 0;

static std::wstring IniPathBesideExecutable()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring path(modulePath, length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        path.resize(separator + 1);
    } else {
        path.clear();
    }
    return path + L"SC4EdImGui.ini";
}

static std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>((std::max)(0, size)), '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
        result.pop_back();
    }
    return result;
}

static int ReadIniInt(const wchar_t* key, int fallback)
{
    wchar_t value[32] = {};
    wchar_t defaultValue[32] = {};
    _itow_s(fallback, defaultValue, 10);
    GetPrivateProfileStringW(L"SC4Ed", key, defaultValue, value, _countof(value), g_iniPath.c_str());
    return _wtoi(value);
}

static void WriteIniInt(const wchar_t* key, int value)
{
    wchar_t text[32] = {};
    _itow_s(value, text, 10);
    WritePrivateProfileStringW(L"SC4Ed", key, text, g_iniPath.c_str());
}

static SavedWindowPlacement LoadWindowPlacement(float scale)
{
    SavedWindowPlacement saved;
    saved.width = static_cast<int>(1440 * scale);
    saved.height = static_cast<int>(900 * scale);
    if (!ReadIniInt(L"WindowSaved", 0)) return saved;

    saved.x = ReadIniInt(L"WindowX", saved.x);
    saved.y = ReadIniInt(L"WindowY", saved.y);
    saved.width = (std::max)(640, ReadIniInt(L"WindowWidth", saved.width));
    saved.height = (std::max)(480, ReadIniInt(L"WindowHeight", saved.height));
    saved.maximized = ReadIniInt(L"WindowMaximized", 0) != 0;

    RECT rect = { saved.x, saved.y, saved.x + saved.width, saved.y + saved.height };
    MONITORINFO monitor = { sizeof(monitor) };
    if (GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
        const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        saved.width = (std::min)(saved.width, workWidth);
        saved.height = (std::min)(saved.height, workHeight);
        saved.x = std::clamp(saved.x, static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - saved.width);
        saved.y = std::clamp(saved.y, static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - saved.height);
    }
    return saved;
}

static void CaptureUiSettings(HWND hwnd, const EditorState& state)
{
    WINDOWPLACEMENT placement = { sizeof(placement) };
    if (GetWindowPlacement(hwnd, &placement)) {
        const RECT& rect = placement.rcNormalPosition;
        g_exitWindowPlacement.x = rect.left;
        g_exitWindowPlacement.y = rect.top;
        g_exitWindowPlacement.width = rect.right - rect.left;
        g_exitWindowPlacement.height = rect.bottom - rect.top;
        g_exitWindowPlacement.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    }
    g_exitToolTab = state.activeToolTab;
    g_exitEventPaletteTab = state.activeEventPaletteTab;
    g_exitSpriteTab = state.activeSpriteTab;
    g_uiSettingsCaptured = true;
}

static void WriteCapturedUiSettings()
{
    if (!g_uiSettingsCaptured) return;
    WriteIniInt(L"WindowSaved", 1);
    WriteIniInt(L"WindowX", g_exitWindowPlacement.x);
    WriteIniInt(L"WindowY", g_exitWindowPlacement.y);
    WriteIniInt(L"WindowWidth", g_exitWindowPlacement.width);
    WriteIniInt(L"WindowHeight", g_exitWindowPlacement.height);
    WriteIniInt(L"WindowMaximized", g_exitWindowPlacement.maximized ? 1 : 0);
    WriteIniInt(L"ToolTab", g_exitToolTab);
    WriteIniInt(L"EventPaletteTab", g_exitEventPaletteTab);
    WriteIniInt(L"SpriteTab", g_exitSpriteTab);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_CLOSE:
        if (g_editorState && !ConfirmUnsavedChanges(*g_editorState, hwnd, "closing")) {
            return 0;
        }
        if (g_editorState) {
            CaptureUiSettings(hwnd, *g_editorState);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            return 0;
        }
        g_d3d.Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        if (!g_uiSettingsCaptured && g_editorState) {
            CaptureUiSettings(hwnd, *g_editorState);
        }
        PostQuitMessage(0);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(drop, i, path, MAX_PATH) > 0) {
                g_droppedFiles.emplace_back(path);
            }
        }
        DragFinish(drop);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    g_iniPath = IniPathBesideExecutable();
    g_iniPathUtf8 = WideToUtf8(g_iniPath);
    const SavedWindowPlacement savedWindow = LoadWindowPlacement(scale);

   // WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, instance, nullptr, nullptr, nullptr, nullptr, L"SC4EdImGui", nullptr };
      WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, instance, 
                       LoadIconW(instance, MAKEINTRESOURCEW(IDI_SC4EDIMGUI)), LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
                       nullptr, nullptr, L"SC4EdImGui", LoadIconW(instance, MAKEINTRESOURCEW(IDI_SC4EDIMGUI)) 
                       };


    RegisterClassExW(&wc);    
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"SC4Ed ImGui", WS_OVERLAPPEDWINDOW,
        savedWindow.x, savedWindow.y, savedWindow.width, savedWindow.height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_d3d.Create(hwnd)) {
        g_d3d.Cleanup();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, savedWindow.maximized ? SW_SHOWMAXIMIZED : showCommand);
    UpdateWindow(hwnd);
    DragAcceptFiles(hwnd, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = g_iniPathUtf8.c_str();
    ApplySc4Style();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_d3d.Device(), g_d3d.Context());

    EditorState state;
    state.activeToolTab = std::clamp(ReadIniInt(L"ToolTab", 0), 0, 7);
    state.activeEventPaletteTab = std::clamp(ReadIniInt(L"EventPaletteTab", 0), 0, 2);
    state.activeSpriteTab = std::clamp(ReadIniInt(L"SpriteTab", 0), 0, 1);
    g_editorState = &state;
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done || !g_d3d.BeginFrame()) {
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawEditorUi(state, hwnd, g_d3d.Device(), g_droppedFiles);
        g_droppedFiles.clear();

        ImGui::Render();
        g_d3d.RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_d3d.Present();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::SaveIniSettingsToDisk(g_iniPathUtf8.c_str());
    ImGui::DestroyContext();
    WriteCapturedUiSettings();

    Emulator::Instance()->Terminate();
    g_editorState = nullptr;
    g_d3d.Cleanup();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}
