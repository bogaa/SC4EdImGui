#include "D3D11Host.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include <dxgi.h>

bool D3D11Host::Create(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 2, D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &featureLevel, &context_);
    if (result == DXGI_ERROR_UNSUPPORTED) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, featureLevels, 2, D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &featureLevel, &context_);
    }
    if (FAILED(result)) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void D3D11Host::Cleanup()
{
    CleanupRenderTarget();
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void D3D11Host::Resize(UINT width, UINT height)
{
    resizeWidth_ = width;
    resizeHeight_ = height;
}

bool D3D11Host::BeginFrame()
{
    if (swapChainOccluded_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
        return false;
    }
    swapChainOccluded_ = false;

    if (resizeWidth_ != 0 && resizeHeight_ != 0) {
        CleanupRenderTarget();
        swapChain_->ResizeBuffers(0, resizeWidth_, resizeHeight_, DXGI_FORMAT_UNKNOWN, 0);
        resizeWidth_ = resizeHeight_ = 0;
        CreateRenderTarget();
    }

    return true;
}

void D3D11Host::RenderDrawData(ImDrawData* drawData)
{
    const float clearColor[4] = { 0.06f, 0.07f, 0.08f, 1.0f };
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    context_->ClearRenderTargetView(renderTarget_, clearColor);
    ImGui_ImplDX11_RenderDrawData(drawData);
}

bool D3D11Host::Present()
{
    const HRESULT result = swapChain_->Present(1, 0);
    swapChainOccluded_ = (result == DXGI_STATUS_OCCLUDED);
    return !FAILED(result);
}

void D3D11Host::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
    backBuffer->Release();
}

void D3D11Host::CleanupRenderTarget()
{
    if (renderTarget_) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
}
