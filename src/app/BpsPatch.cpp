#include "BpsPatch.h"

#include <fstream>
#include <iterator>
#include <limits>

namespace {

constexpr uint64_t kSourceRead = 0;
constexpr uint64_t kTargetRead = 1;

void AppendNumber(std::vector<uint8_t>& output, uint64_t value)
{
    while (true) {
        const uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
        value >>= 7;
        if (value == 0) {
            output.push_back(static_cast<uint8_t>(byte | 0x80u));
            return;
        }
        output.push_back(byte);
        --value;
    }
}

uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

uint32_t Crc32(const std::vector<uint8_t>& data)
{
    return Crc32(data.data(), data.size());
}

void AppendCrc32(std::vector<uint8_t>& output, uint32_t crc)
{
    output.push_back(static_cast<uint8_t>(crc & 0xFFu));
    output.push_back(static_cast<uint8_t>((crc >> 8) & 0xFFu));
    output.push_back(static_cast<uint8_t>((crc >> 16) & 0xFFu));
    output.push_back(static_cast<uint8_t>((crc >> 24) & 0xFFu));
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& bytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.good() || stream.eof();
}

}

bool CreateBpsPatch(
    const std::vector<uint8_t>& source,
    const std::vector<uint8_t>& target,
    std::vector<uint8_t>& patch,
    std::string& error)
{
    patch.clear();
    error.clear();
    if (source.empty()) {
        error = "The original ROM is empty.";
        return false;
    }
    if (target.empty()) {
        error = "The edited ROM is empty.";
        return false;
    }
    if (source.size() > static_cast<size_t>((std::numeric_limits<uint64_t>::max)())
        || target.size() > static_cast<size_t>((std::numeric_limits<uint64_t>::max)())) {
        error = "The ROM is too large for a BPS patch.";
        return false;
    }

    patch.insert(patch.end(), { 'B', 'P', 'S', '1' });
    AppendNumber(patch, static_cast<uint64_t>(source.size()));
    AppendNumber(patch, static_cast<uint64_t>(target.size()));
    AppendNumber(patch, 0); // No metadata.

    size_t offset = 0;
    while (offset < target.size()) {
        const bool sourceMatches = offset < source.size() && source[offset] == target[offset];
        const size_t runStart = offset;
        if (sourceMatches) {
            while (offset < target.size() && offset < source.size() && source[offset] == target[offset]) {
                ++offset;
            }
            AppendNumber(patch, ((static_cast<uint64_t>(offset - runStart) - 1u) << 2) | kSourceRead);
        } else {
            while (offset < target.size() && !(offset < source.size() && source[offset] == target[offset])) {
                ++offset;
            }
            AppendNumber(patch, ((static_cast<uint64_t>(offset - runStart) - 1u) << 2) | kTargetRead);
            patch.insert(patch.end(), target.begin() + runStart, target.begin() + offset);
        }
    }

    AppendCrc32(patch, Crc32(source));
    AppendCrc32(patch, Crc32(target));
    AppendCrc32(patch, Crc32(patch));
    return true;
}

bool ExportBpsPatch(
    const std::string& sourcePath,
    const std::vector<uint8_t>& target,
    const std::string& patchPath,
    std::string& error)
{
    std::vector<uint8_t> source;
    if (!ReadFileBytes(sourcePath, source)) {
        error = "Could not read the original ROM.";
        return false;
    }

    std::vector<uint8_t> patch;
    if (!CreateBpsPatch(source, target, patch, error)) {
        return false;
    }

    std::ofstream stream(patchPath, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "Could not create the BPS file.";
        return false;
    }
    stream.write(reinterpret_cast<const char*>(patch.data()), static_cast<std::streamsize>(patch.size()));
    if (!stream) {
        error = "Could not finish writing the BPS file.";
        return false;
    }
    return true;
}
