#include "BpsPatch.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

uint64_t ReadNumber(const std::vector<uint8_t>& patch, size_t& offset)
{
    uint64_t value = 0;
    uint64_t shift = 1;
    while (offset < patch.size()) {
        const uint8_t byte = patch[offset++];
        value += static_cast<uint64_t>(byte & 0x7Fu) * shift;
        if (byte & 0x80u) {
            return value;
        }
        shift <<= 7;
        value += shift;
    }
    throw std::runtime_error("truncated BPS number");
}

std::vector<uint8_t> ApplyCreatedPatch(const std::vector<uint8_t>& source, const std::vector<uint8_t>& patch)
{
    if (patch.size() < 16 || patch[0] != 'B' || patch[1] != 'P' || patch[2] != 'S' || patch[3] != '1') {
        throw std::runtime_error("bad BPS header");
    }

    size_t offset = 4;
    if (ReadNumber(patch, offset) != source.size()) {
        throw std::runtime_error("wrong source size");
    }
    const size_t targetSize = static_cast<size_t>(ReadNumber(patch, offset));
    const size_t metadataSize = static_cast<size_t>(ReadNumber(patch, offset));
    if (metadataSize > patch.size() - offset) {
        throw std::runtime_error("bad metadata size");
    }
    offset += metadataSize;

    std::vector<uint8_t> target;
    target.reserve(targetSize);
    while (target.size() < targetSize) {
        if (offset >= patch.size() - 12) {
            throw std::runtime_error("truncated BPS actions");
        }
        const uint64_t action = ReadNumber(patch, offset);
        const size_t length = static_cast<size_t>((action >> 2) + 1);
        switch (action & 3u) {
        case 0:
            if (target.size() + length > source.size()) {
                throw std::runtime_error("source read outside source");
            }
            target.insert(target.end(), source.begin() + target.size(), source.begin() + target.size() + length);
            break;
        case 1:
            if (length > patch.size() - 12 - offset) {
                throw std::runtime_error("target read outside patch");
            }
            target.insert(target.end(), patch.begin() + offset, patch.begin() + offset + length);
            offset += length;
            break;
        default:
            throw std::runtime_error("unexpected action from simple BPS writer");
        }
    }
    return target;
}

bool CheckRoundTrip(const std::vector<uint8_t>& source, const std::vector<uint8_t>& target)
{
    std::vector<uint8_t> patch;
    std::string error;
    return CreateBpsPatch(source, target, patch, error)
        && ApplyCreatedPatch(source, patch) == target;
}

}

int main()
{
    try {
        const std::vector<uint8_t> source = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        if (!CheckRoundTrip(source, source)
            || !CheckRoundTrip(source, { 0, 1, 20, 30, 4, 5, 6, 70, 80, 9 })
            || !CheckRoundTrip(source, { 0, 1, 2, 3, 4 })
            || !CheckRoundTrip(source, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 })) {
            std::cerr << "BPS round-trip failed\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
