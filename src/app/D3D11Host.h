#pragma once

#include <windows.h>
#include <d3d11.h>

struct ImDrawData;

class D3D11Host {
public:
    bool Create(HWND hwnd);
    void Cleanup();
    void Resize(UINT width, UINT height);
    bool BeginFrame();
    void RenderDrawData(ImDrawData* drawData);
    bool Present();

    ID3D11Device* Device() const { return device_; }
    ID3D11DeviceContext* Context() const { return context_; }

private:
    void CreateRenderTarget();
    void CleanupRenderTarget();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
    UINT resizeWidth_ = 0;
    UINT resizeHeight_ = 0;
    bool swapChainOccluded_ = false;
};
