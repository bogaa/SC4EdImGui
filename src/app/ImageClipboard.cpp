#include "ImageClipboard.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <objidl.h>
#include <wincodec.h>

namespace {

UINT PngClipboardFormat()
{
    static const UINT format = RegisterClipboardFormatW(L"PNG");
    return format;
}

HGLOBAL CreatePng(const ClipboardImage& image)
{
    IWICImagingFactory* factory = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    IStream* stream = nullptr;
    HGLOBAL handle = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(nullptr, FALSE, &stream);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(hr)) hr = frame->Initialize(properties);
    if (SUCCEEDED(hr)) hr = frame->SetSize(image.width, image.height);
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pixelFormat);
    if (SUCCEEDED(hr) && pixelFormat != GUID_WICPixelFormat32bppBGRA) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        hr = frame->WritePixels(
            image.height,
            image.width * sizeof(uint32_t),
            static_cast<UINT>(image.pixels.size() * sizeof(uint32_t)),
            reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image.pixels.data())));
    }
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream, &handle);

    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (factory) factory->Release();
    if (stream) stream->Release();
    if (FAILED(hr) && handle) {
        GlobalFree(handle);
        handle = nullptr;
    }
    return handle;
}

bool ReadPng(HGLOBAL handle, ClipboardImage& image)
{
    if (!handle) return false;
    BYTE* data = static_cast<BYTE*>(GlobalLock(handle));
    const SIZE_T size = GlobalSize(handle);
    if (!data || !size || size > UINT_MAX) {
        if (data) GlobalUnlock(handle);
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT width = 0;
    UINT height = 0;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(data, static_cast<DWORD>(size));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(hr) && width && height && width <= INT_MAX && height <= INT_MAX) {
        image.width = static_cast<int>(width);
        image.height = static_cast<int>(height);
        image.pixels.resize(static_cast<size_t>(width) * height);
        hr = converter->CopyPixels(
            nullptr,
            width * sizeof(uint32_t),
            static_cast<UINT>(image.pixels.size() * sizeof(uint32_t)),
            reinterpret_cast<BYTE*>(image.pixels.data()));
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    GlobalUnlock(handle);
    if (FAILED(hr)) image = {};
    return SUCCEEDED(hr);
}

bool ReadDib(HGLOBAL handle, ClipboardImage& image)
{
    if (!handle) return false;
    const BYTE* dib = static_cast<const BYTE*>(GlobalLock(handle));
    if (!dib) return false;

    const BITMAPINFOHEADER* header = reinterpret_cast<const BITMAPINFOHEADER*>(dib);
    const int width = header->biWidth;
    const int height = std::abs(header->biHeight);
    const bool topDown = header->biHeight < 0;
    if (header->biSize < sizeof(BITMAPINFOHEADER) || width <= 0 || height <= 0 ||
        (header->biBitCount != 24 && header->biBitCount != 32)) {
        GlobalUnlock(handle);
        return false;
    }

    DWORD colorCount = header->biClrUsed;
    if (!colorCount && header->biBitCount <= 8) colorCount = 1u << header->biBitCount;
    DWORD masksSize = 0;
    if (header->biSize == sizeof(BITMAPINFOHEADER) && header->biCompression == BI_BITFIELDS) {
        masksSize = 3 * sizeof(DWORD);
    }
    const BYTE* bits = dib + header->biSize + masksSize + colorCount * sizeof(RGBQUAD);
    const size_t stride = (static_cast<size_t>(width) * header->biBitCount + 31u) / 32u * 4u;

    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<size_t>(width) * height, 0);
    bool hasAlpha = false;
    for (int y = 0; y < height; ++y) {
        const BYTE* row = bits + static_cast<size_t>(topDown ? y : height - 1 - y) * stride;
        for (int x = 0; x < width; ++x) {
            const BYTE b = row[x * (header->biBitCount / 8) + 0];
            const BYTE g = row[x * (header->biBitCount / 8) + 1];
            const BYTE r = row[x * (header->biBitCount / 8) + 2];
            const BYTE a = header->biBitCount == 32 ? row[x * 4 + 3] : 0xFF;
            hasAlpha |= a != 0;
            image.pixels[static_cast<size_t>(y) * width + x] =
                (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) | b;
        }
    }
    GlobalUnlock(handle);

    // CF_DIB producers often leave the unused alpha byte at zero.
    if (header->biBitCount == 32 && !hasAlpha) {
        for (uint32_t& pixel : image.pixels) pixel |= 0xFF000000u;
    }
    return true;
}

} // namespace

bool CopyImageToClipboard(HWND hwnd, const ClipboardImage& image)
{
    if (!hwnd || image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<size_t>(image.width) * image.height) {
        return false;
    }

    const SIZE_T pixelsSize = static_cast<SIZE_T>(image.width) * image.height * sizeof(uint32_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + pixelsSize);
    if (!handle) return false;
    BYTE* dib = static_cast<BYTE*>(GlobalLock(handle));
    if (!dib) {
        GlobalFree(handle);
        return false;
    }

    BITMAPV5HEADER* header = reinterpret_cast<BITMAPV5HEADER*>(dib);
    ZeroMemory(header, sizeof(*header));
    header->bV5Size = sizeof(*header);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(pixelsSize);
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    std::memcpy(dib + sizeof(*header), image.pixels.data(), pixelsSize);
    GlobalUnlock(handle);

    HGLOBAL pngHandle = CreatePng(image);
    if (!OpenClipboard(hwnd)) {
        GlobalFree(handle);
        if (pngHandle) GlobalFree(pngHandle);
        return false;
    }
    EmptyClipboard();
    const bool dibSet = SetClipboardData(CF_DIBV5, handle) != nullptr;
    const bool pngSet = pngHandle && SetClipboardData(PngClipboardFormat(), pngHandle) != nullptr;
    if (!dibSet) {
        GlobalFree(handle);
    }
    if (pngHandle && !pngSet) GlobalFree(pngHandle);
    CloseClipboard();
    return dibSet || pngSet;
}

bool ReadImageFromClipboard(HWND hwnd, ClipboardImage& image)
{
    image = {};
    if (!hwnd || !OpenClipboard(hwnd)) return false;

    bool ok = false;
    if (IsClipboardFormatAvailable(PngClipboardFormat())) {
        ok = ReadPng(static_cast<HGLOBAL>(GetClipboardData(PngClipboardFormat())), image);
    }
    if (IsClipboardFormatAvailable(CF_DIBV5)) {
        if (!ok) ok = ReadDib(static_cast<HGLOBAL>(GetClipboardData(CF_DIBV5)), image);
    }
    if (!ok && IsClipboardFormatAvailable(CF_DIB)) {
        ok = ReadDib(static_cast<HGLOBAL>(GetClipboardData(CF_DIB)), image);
    }
    CloseClipboard();
    return ok;
}
