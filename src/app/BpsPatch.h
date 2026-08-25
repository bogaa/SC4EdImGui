#pragma once

#include <cstdint>
#include <string>
#include <vector>

bool CreateBpsPatch(
    const std::vector<uint8_t>& source,
    const std::vector<uint8_t>& target,
    std::vector<uint8_t>& patch,
    std::string& error);

bool ExportBpsPatch(
    const std::string& sourcePath,
    const std::vector<uint8_t>& target,
    const std::string& patchPath,
    std::string& error);
