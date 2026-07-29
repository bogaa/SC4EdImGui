#pragma once

#include <cstdint>
#include <string>

struct RomInfo {
    std::string path;
    uintmax_t size = 0;
    uint32_t checksum = 0;
    bool loaded = false;
    bool hasHeader = false;
};

bool LoadRomInfo(const std::string& path, RomInfo& out);
