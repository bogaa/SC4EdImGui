#include "RomInfo.h"

#include <fstream>
#include <vector>

static uint32_t Crc32(const std::vector<uint8_t>& bytes)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool LoadRomInfo(const std::string& path, RomInfo& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    const auto size = file.tellg();
    if (size <= 0) {
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return false;
    }

    out.path = path;
    out.size = bytes.size();
    out.hasHeader = (bytes.size() % 0x8000) == 0x200;
    if (out.hasHeader && bytes.size() > 0x200) {
        bytes.erase(bytes.begin(), bytes.begin() + 0x200);
    }
    out.checksum = Crc32(bytes);
    out.loaded = true;
    return true;
}
