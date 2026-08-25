#pragma once

#include <cstdint>
#include <vector>

#include <windows.h>

struct ClipboardImage {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
};

bool CopyImageToClipboard(HWND hwnd, const ClipboardImage& image);
bool ReadImageFromClipboard(HWND hwnd, ClipboardImage& image);
