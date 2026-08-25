#include "InstrumentEditor.h"

#include "CompressionCore.h"
#include "EditorState.h"
#include "EditorUndo.h"
#include "SC4Core.h"
#include "imgui.h"

#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr unsigned kDirectoryPacketPc = 0xE0000;
constexpr unsigned kSamplePacketPc = 0xEAD35;
constexpr unsigned kSamplePacketSize = 0x2F80;
constexpr unsigned kSampleAramStart = 0x5000;
constexpr unsigned kCompressedAramStart = 0x7F80;
constexpr unsigned kDirectoryAram = 0xFE00;
constexpr int kInstrumentCount = 20;
constexpr int kDspRate = 32000;

const char* InstrumentName(int id) {
    static const char* names[kInstrumentCount] = {
        "Simon's clarinet", "Bass", "Toy", "Piano", "Boss instrument",
        "Flute", "Horn", "Bright organ", "Church organ", "Dying bat",
        "Bright wooden drum", "Wooden drum", "Steel drum", "Drum", "Vine sound FX",
        "Bright synth 1", "Bright synth 2", "Death scream", "Unknown 18", "Unknown 19"
    };
    return id >= 0 && id < kInstrumentCount ? names[id] : "Unknown";
}

struct WavData {
    int sampleRate = 0;
    std::vector<float> samples;
    int loopStart = -1;
    int loopEnd = -1;
};

struct InstrumentInfo {
    uint16_t start = 0;
    uint16_t loop = 0;
    int blocks = 0;
    int capacityBlocks = 0;
    int loopBlock = 0;
    bool looping = false;
    std::vector<int16_t> preview;
};

struct State {
    const uint8_t* rom = nullptr;
    int selected = 0;
    std::array<InstrumentInfo, kInstrumentCount> instruments;
    std::string importedPath;
    std::string status;
    bool fitToSlot = true;
};

State g;

uint16_t Read16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t Read32(const uint8_t* data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

void Write16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
}

std::string OpenWavDialog(HWND hwnd) {
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Wave file (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "wav";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Replace CV4 instrument sample";
    return GetOpenFileNameA(&ofn) ? path : std::string();
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), {});
    return input.good() || input.eof();
}

bool DecodeWav(const std::string& path, WavData& wav, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) { error = "Could not read the WAV file."; return false; }
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) {
        error = "The file is not a RIFF/WAVE file."; return false;
    }

    uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmSize = 0;
    for (size_t at = 12; at + 8 <= bytes.size();) {
        const uint32_t size = Read32(bytes.data() + at + 4);
        const size_t dataAt = at + 8;
        if (dataAt + size > bytes.size()) break;
        if (!std::memcmp(bytes.data() + at, "fmt ", 4) && size >= 16) {
            format = Read16(bytes.data() + dataAt);
            channels = Read16(bytes.data() + dataAt + 2);
            rate = Read32(bytes.data() + dataAt + 4);
            blockAlign = Read16(bytes.data() + dataAt + 12);
            bits = Read16(bytes.data() + dataAt + 14);
        } else if (!std::memcmp(bytes.data() + at, "data", 4)) {
            pcm = bytes.data() + dataAt;
            pcmSize = size;
        } else if (!std::memcmp(bytes.data() + at, "smpl", 4) && size >= 60) {
            const uint32_t loopCount = Read32(bytes.data() + dataAt + 28);
            if (loopCount > 0) {
                wav.loopStart = static_cast<int>(Read32(bytes.data() + dataAt + 44));
                wav.loopEnd = static_cast<int>(Read32(bytes.data() + dataAt + 48));
            }
        }
        at = dataAt + size + (size & 1);
    }
    if (!pcm || !channels || !rate || !blockAlign || (format != 1 && format != 3)) {
        error = "Use an uncompressed PCM or 32-bit float WAV file."; return false;
    }
    if ((format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) || (format == 3 && bits != 32)) {
        error = "Unsupported WAV bit depth."; return false;
    }

    const size_t frames = pcmSize / blockAlign;
    wav.sampleRate = static_cast<int>(rate);
    wav.samples.assign(frames, 0.0f);
    const int bytesPerSample = bits / 8;
    for (size_t frame = 0; frame < frames; ++frame) {
        double mixed = 0.0;
        for (unsigned channel = 0; channel < channels; ++channel) {
            const uint8_t* source = pcm + frame * blockAlign + channel * bytesPerSample;
            float value = 0.0f;
            if (format == 3) {
                std::memcpy(&value, source, sizeof(value));
            } else if (bits == 8) {
                value = (static_cast<int>(source[0]) - 128) / 128.0f;
            } else if (bits == 16) {
                value = static_cast<int16_t>(Read16(source)) / 32768.0f;
            } else if (bits == 24) {
                int32_t v = source[0] | (source[1] << 8) | (source[2] << 16);
                if (v & 0x800000) v |= ~0xFFFFFF;
                value = v / 8388608.0f;
            } else {
                value = static_cast<int32_t>(Read32(source)) / 2147483648.0f;
            }
            mixed += (std::max)(-1.0f, (std::min)(1.0f, value));
        }
        wav.samples[frame] = static_cast<float>(mixed / channels);
    }
    if (wav.samples.empty()) { error = "The WAV file contains no samples."; return false; }
    return true;
}

std::vector<int16_t> Resample(const WavData& wav, size_t maximumSamples, bool fitToSlot, bool& fitted) {
    fitted = false;
    const double nativeCount = wav.samples.size() * (double(kDspRate) / wav.sampleRate);
    size_t outputCount = (std::max)(size_t(1), static_cast<size_t>(std::llround(nativeCount)));
    if (outputCount > maximumSamples) {
        if (!fitToSlot) return {};
        outputCount = maximumSamples;
        fitted = true;
    }
    std::vector<int16_t> out(outputCount);
    const double scale = double(wav.sampleRate) / kDspRate;
    for (size_t i = 0; i < outputCount; ++i) {
        const double source = i * scale;
        const size_t a = (std::min)(static_cast<size_t>(source), wav.samples.size() - 1);
        const size_t b = (std::min)(a + 1, wav.samples.size() - 1);
        const float value = wav.samples[a] + (wav.samples[b] - wav.samples[a]) * static_cast<float>(source - a);
        // BRR is decoded as a signed 15-bit signal and doubled by the SNES DSP output stage.
        out[i] = static_cast<int16_t>(std::lround((std::max)(-1.0f, (std::min)(1.0f, value)) * 16383.0f));
    }
    return out;
}

int Clamp16(int value) { return (std::max)(-32768, (std::min)(32767, value)); }

int Predict(int filter, int previous, int previous2) {
    switch (filter) {
    case 1: return previous + ((-previous) >> 4);
    case 2: return (previous << 1) + ((-3 * previous) >> 5) - previous2 + (previous2 >> 4);
    case 3: return (previous << 1) + ((-13 * previous) >> 6) - previous2 + ((3 * previous2) >> 4);
    default: return 0;
    }
}

std::vector<uint8_t> EncodeBrr(const std::vector<int16_t>& input, bool looping, int loopBlock) {
    const int blocks = static_cast<int>((input.size() + 15) / 16);
    std::vector<uint8_t> encoded(static_cast<size_t>(blocks) * 9, 0);
    int previous = 0, previous2 = 0;
    for (int block = 0; block < blocks; ++block) {
        long long bestError = (std::numeric_limits<long long>::max)();
        int bestFilter = 0, bestShift = 0, bestPrevious = previous, bestPrevious2 = previous2;
        std::array<int8_t, 16> bestNibbles = {};
        const int filterEnd = looping && block == loopBlock ? 1 : 4;
        for (int filter = 0; filter < filterEnd; ++filter) {
            for (int shift = 0; shift <= 12; ++shift) {
                int p1 = previous, p2 = previous2;
                long long error = 0;
                std::array<int8_t, 16> nibbles = {};
                for (int i = 0; i < 16; ++i) {
                    const int index = block * 16 + i;
                    const int target = index < static_cast<int>(input.size()) ? input[index] : 0;
                    const int prediction = Predict(filter, p1, p2);
                    const double step = double(1 << shift) / 2.0;
                    const int nibble = (std::max)(-8, (std::min)(7, static_cast<int>(std::lround((target - prediction) / step))));
                    const int decoded = Clamp16(prediction + ((nibble << shift) >> 1));
                    const long long difference = target - decoded;
                    error += difference * difference;
                    nibbles[i] = static_cast<int8_t>(nibble);
                    p2 = p1; p1 = decoded;
                }
                if (error < bestError) {
                    bestError = error; bestFilter = filter; bestShift = shift;
                    bestNibbles = nibbles; bestPrevious = p1; bestPrevious2 = p2;
                }
            }
        }
        uint8_t header = static_cast<uint8_t>((bestShift << 4) | (bestFilter << 2));
        if (block == blocks - 1) header |= 1 | (looping ? 2 : 0);
        encoded[block * 9] = header;
        for (int i = 0; i < 8; ++i) {
            encoded[block * 9 + 1 + i] = static_cast<uint8_t>((bestNibbles[i * 2] & 0xF) << 4) |
                static_cast<uint8_t>(bestNibbles[i * 2 + 1] & 0xF);
        }
        previous = bestPrevious; previous2 = bestPrevious2;
        if (looping && block + 1 == loopBlock) previous = previous2 = 0;
    }
    return encoded;
}

std::vector<uint8_t> CompressSc4(const uint8_t* source, unsigned size) {
    std::vector<uint8_t> packed;
    packed.reserve(size + size / 31 + 1);
    auto repeatedLength = [&](unsigned offset) {
        unsigned length = 1;
        while (length < 33 && offset + length < size && source[offset + length] == source[offset]) ++length;
        return length;
    };
    auto zeroPairLength = [&](unsigned offset) {
        if (source[offset] || offset + 1 >= size) return 0u;
        const uint8_t value = source[offset + 1];
        unsigned pairs = 0;
        while (pairs < 33 && offset + pairs * 2 + 1 < size && !source[offset + pairs * 2] && source[offset + pairs * 2 + 1] == value) ++pairs;
        return pairs;
    };
    auto bestMatch = [&](unsigned offset, unsigned& bestSource) {
        unsigned bestLength = 0; bestSource = 0;
        const unsigned windowStart = offset > 0x400 ? offset - 0x400 : 0;
        for (unsigned candidate = windowStart; candidate < offset; ++candidate) {
            unsigned resolved = (offset & 0xFC00) | (candidate & 0x3FF);
            if (resolved >= offset) resolved -= 0x400;
            if (resolved != candidate) continue;
            unsigned length = 0;
            while (length < 33 && offset + length < size && source[candidate + length] == source[offset + length]) ++length;
            if (length > bestLength) { bestLength = length; bestSource = candidate; }
        }
        return bestLength;
    };
    for (unsigned offset = 0; offset < size;) {
        const unsigned repeat = repeatedLength(offset);
        if (!source[offset] && repeat >= 2) { packed.push_back(static_cast<uint8_t>(0xE0 | (repeat - 2))); offset += repeat; continue; }
        if (repeat >= 3) { packed.push_back(static_cast<uint8_t>(0xC0 | (repeat - 2))); packed.push_back(source[offset]); offset += repeat; continue; }
        const unsigned pairs = zeroPairLength(offset);
        if (pairs >= 3) { packed.push_back(static_cast<uint8_t>(0xA0 | (pairs - 2))); packed.push_back(source[offset + 1]); offset += pairs * 2; continue; }
        unsigned matchSource = 0;
        const unsigned match = bestMatch(offset, matchSource);
        if (match >= 3) {
            const unsigned value = ((matchSource & 0x3FF) + 0x3DF) & 0x3FF;
            packed.push_back(static_cast<uint8_t>(((match - 2) << 2) | (value >> 8)));
            packed.push_back(static_cast<uint8_t>(value)); offset += match; continue;
        }
        const size_t control = packed.size(); packed.push_back(0x81);
        unsigned literals = 0;
        while (literals < 31 && offset < size) {
            packed.push_back(source[offset++]); ++literals;
            unsigned nextSource = 0;
            if (offset < size && (repeatedLength(offset) >= 3 || zeroPairLength(offset) >= 3 || bestMatch(offset, nextSource) >= 3)) break;
        }
        packed[control] = static_cast<uint8_t>(0x80 | literals);
    }
    return packed;
}

bool BuildAram(SC4Core& core, std::array<uint8_t, 0x10000>& aram, unsigned& packedSize, std::string& error) {
    if (!core.rom || core.romSize < kSamplePacketPc + kSamplePacketSize || core.romSize < kDirectoryPacketPc + 2) {
        error = "This ROM does not contain the expected US CV4 audio bank."; return false;
    }
    packedSize = Read16(core.rom + kDirectoryPacketPc);
    if (packedSize < 3 || kDirectoryPacketPc + packedSize > core.romSize) {
        error = "The CV4 audio directory packet is invalid."; return false;
    }
    aram.fill(0);
    std::memcpy(aram.data() + kSampleAramStart, core.rom + kSamplePacketPc, kSamplePacketSize);
    const int unpacked = GFXRLE(core.rom, aram.data() + kCompressedAramStart,
        kDirectoryPacketPc + 2, packedSize, core.type);
    if (unpacked != 0x8000) { error = "The CV4 audio directory did not unpack to 32 KB."; return false; }
    return true;
}

std::vector<int16_t> DecodeBrr(const std::array<uint8_t, 0x10000>& aram, uint16_t start, bool& looping, int& blocks) {
    std::vector<int16_t> out;
    int previous = 0, previous2 = 0;
    looping = false; blocks = 0;
    unsigned at = start;
    while (at + 9 <= aram.size() && blocks < 4096) {
        const uint8_t header = aram[at++];
        const int shift = header >> 4, filter = (header >> 2) & 3;
        for (int byte = 0; byte < 8; ++byte) {
            const uint8_t value = aram[at++];
            for (int half = 0; half < 2; ++half) {
                int nibble = half ? value & 0xF : value >> 4;
                if (nibble & 8) nibble -= 16;
                const int delta = shift <= 12 ? (nibble << shift) >> 1 : (nibble < 0 ? -2048 : 0);
                const int sample = Clamp16(Predict(filter, previous, previous2) + delta);
                previous2 = previous; previous = sample;
                out.push_back(static_cast<int16_t>(sample));
            }
        }
        ++blocks;
        if (header & 1) { looping = (header & 2) != 0; break; }
    }
    return out;
}

bool LoadState(SC4Core& core, std::string& error) {
    std::array<uint8_t, 0x10000> aram;
    unsigned packedSize = 0;
    if (!BuildAram(core, aram, packedSize, error)) return false;
    for (int id = 0; id < kInstrumentCount; ++id) {
        InstrumentInfo& info = g.instruments[id];
        info.start = Read16(aram.data() + kDirectoryAram + id * 4);
        info.loop = Read16(aram.data() + kDirectoryAram + id * 4 + 2);
        info.preview = DecodeBrr(aram, info.start, info.looping, info.blocks);
        info.capacityBlocks = id + 1 < kInstrumentCount
            ? (Read16(aram.data() + kDirectoryAram + (id + 1) * 4) - info.start) / 9
            : info.blocks;
        info.loopBlock = (info.loop >= info.start) ? (info.loop - info.start) / 9 : 0;
    }
    g.rom = core.rom;
    return true;
}

bool ReplaceInstrument(EditorState& state, int id, const std::string& path, std::string& message) {
    SC4Core& core = state.session.Core();
    std::array<uint8_t, 0x10000> aram;
    unsigned oldPackedSize = 0;
    if (!BuildAram(core, aram, oldPackedSize, message)) return false;
    const uint16_t start = Read16(aram.data() + kDirectoryAram + id * 4);
    const uint16_t next = id + 1 < kInstrumentCount ? Read16(aram.data() + kDirectoryAram + (id + 1) * 4) : 0;
    bool oldLooping = false; int oldBlocks = 0;
    DecodeBrr(aram, start, oldLooping, oldBlocks);
    const int capacityBlocks = id + 1 < kInstrumentCount ? (next - start) / 9 : oldBlocks;
    if (capacityBlocks <= 0) { message = "The instrument has no writable BRR slot."; return false; }

    WavData wav;
    if (!DecodeWav(path, wav, message)) return false;
    bool fitted = false;
    std::vector<int16_t> pcm = Resample(wav, static_cast<size_t>(capacityBlocks) * 16, g.fitToSlot, fitted);
    if (pcm.empty()) {
        message = "The 32 kHz sample needs more than " + std::to_string(capacityBlocks) + " BRR blocks. Enable Fit WAV to slot.";
        return false;
    }
    const uint16_t oldLoop = Read16(aram.data() + kDirectoryAram + id * 4 + 2);
    int loopBlock = oldLoop >= start ? (oldLoop - start) / 9 : 0;
    bool looping = oldLooping;
    if (wav.loopStart >= 0 && wav.loopEnd >= wav.loopStart) {
        const double convertedLoop = wav.loopStart * (double(kDspRate) / wav.sampleRate);
        loopBlock = static_cast<int>(std::llround(convertedLoop / 16.0));
        looping = true;
    }
    loopBlock = std::clamp(loopBlock, 0, (std::max)(0, static_cast<int>((pcm.size() + 15) / 16) - 1));
    std::vector<uint8_t> brr = EncodeBrr(pcm, looping, loopBlock);
    if (brr.size() > static_cast<size_t>(capacityBlocks) * 9) { message = "Encoded BRR data exceeded its slot."; return false; }
    std::fill(aram.begin() + start, aram.begin() + start + capacityBlocks * 9, 0);
    std::copy(brr.begin(), brr.end(), aram.begin() + start);
    Write16(aram.data() + kDirectoryAram + id * 4 + 2, static_cast<uint16_t>(start + loopBlock * 9));

    const std::vector<uint8_t> compressed = CompressSc4(aram.data() + kCompressedAramStart, 0x8000);
    const unsigned newPackedSize = static_cast<unsigned>(compressed.size()) + 2;
    if (newPackedSize > oldPackedSize) {
        message = "The recompressed audio bank needs " + std::to_string(newPackedSize) + " bytes but its ROM packet reserves " +
            std::to_string(oldPackedSize) + ". Try a shorter or simpler WAV.";
        return false;
    }

    std::vector<uint8_t> verify(newPackedSize);
    Write16(verify.data(), static_cast<uint16_t>(newPackedSize));
    std::copy(compressed.begin(), compressed.end(), verify.begin() + 2);
    std::array<uint8_t, 0x8000> unpacked = {};
    const int verifySize = GFXRLE(verify.data(), unpacked.data(), 2, newPackedSize, core.type);
    if (verifySize != 0x8000 || std::memcmp(unpacked.data(), aram.data() + kCompressedAramStart, 0x8000)) {
        message = "Audio recompression verification failed; the ROM was not changed."; return false;
    }

    RomUndoSnapshot before = state.session.CreateUndoSnapshot(state.selectedEventIndex);
    std::memcpy(core.rom + kSamplePacketPc, aram.data() + kSampleAramStart, kSamplePacketSize);
    Write16(core.rom + kDirectoryPacketPc, static_cast<uint16_t>(newPackedSize));
    std::memcpy(core.rom + kDirectoryPacketPc + 2, compressed.data(), compressed.size());
    if (newPackedSize < oldPackedSize) std::memset(core.rom + kDirectoryPacketPc + newPackedSize, 0xFF, oldPackedSize - newPackedSize);
    CommitUndoSnapshot(state, std::move(before));

    g.rom = nullptr;
    message = "Replaced instrument " + std::to_string(id) + " (" + InstrumentName(id) + ") with " +
        std::to_string(brr.size() / 9) + "/" + std::to_string(capacityBlocks) + " BRR blocks" +
        (wav.loopStart >= 0 ? "; WAV loop starts at BRR block " + std::to_string(loopBlock) : std::string()) +
        (fitted ? "; excess audio was trimmed without changing pitch." : ".");
    return true;
}

void DrawWaveform(const std::vector<int16_t>& samples, ImVec2 size) {
    ImGui::InvisibleButton("##waveform", size);
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, IM_COL32(11, 13, 15, 255));
    draw->AddRect(min, max, IM_COL32(70, 78, 82, 255));
    const float center = (min.y + max.y) * 0.5f;
    draw->AddLine(ImVec2(min.x, center), ImVec2(max.x, center), IM_COL32(55, 60, 63, 255));
    if (samples.empty()) return;
    const int columns = (std::max)(1, static_cast<int>(size.x));
    for (int x = 0; x < columns; ++x) {
        const size_t begin = x * samples.size() / columns;
        const size_t end = (std::max)(begin + 1, (x + 1) * samples.size() / columns);
        int16_t low = 32767, high = -32768;
        for (size_t i = begin; i < (std::min)(end, samples.size()); ++i) { low = (std::min)(low, samples[i]); high = (std::max)(high, samples[i]); }
        const float y1 = center - (high / 32768.0f) * size.y * 0.45f;
        const float y2 = center - (low / 32768.0f) * size.y * 0.45f;
        draw->AddLine(ImVec2(min.x + x, y1), ImVec2(min.x + x, y2), IM_COL32(81, 202, 155, 255));
    }
}

} // namespace

void ResetInstrumentEditor() { g = {}; }

void DrawInstrumentEditor(EditorState& state, HWND hwnd, std::string& logMessage) {
    if (!state.session.IsLoaded()) { ImGui::TextUnformatted("Load a ROM to edit instruments."); return; }
    SC4Core& core = state.session.Core();
    if (g.rom != core.rom) {
        std::string error;
        if (!LoadState(core, error)) { ImGui::TextWrapped("%s", error.c_str()); return; }
    }

    const float listWidth = 285.0f;
    ImGui::BeginChild("instrument-list", ImVec2(listWidth, 0), true);
    for (int id = 0; id < kInstrumentCount; ++id) {
        char label[96] = {};
        std::snprintf(label, sizeof(label), "%02d  %s", id, InstrumentName(id));
        if (ImGui::Selectable(label, g.selected == id)) g.selected = id;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("instrument-editor", ImVec2(0, 0), false);
    const InstrumentInfo& info = g.instruments[g.selected];
    ImGui::Text("Instrument %d", g.selected);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", InstrumentName(g.selected));
    ImGui::Text("Sample: %u samples (%.3f s at 32 kHz)", static_cast<unsigned>(info.preview.size()), info.preview.size() / 32000.0);
    ImGui::Text("BRR: %d/%d blocks, ARAM $%04X, loop $%04X%s", info.blocks, info.capacityBlocks,
        info.start, info.loop, info.looping ? "" : " (one-shot)");
    DrawWaveform(info.preview, ImVec2((std::max)(160.0f, ImGui::GetContentRegionAvail().x), 150.0f));
    ImGui::Checkbox("Fit WAV to available slot", &g.fitToSlot);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trims excess audio after conversion to 32 kHz; playback pitch is preserved");
    if (ImGui::Button("Load WAV and Replace")) {
        const std::string path = OpenWavDialog(hwnd);
        if (!path.empty()) {
            g.importedPath = path;
            std::string result;
            if (ReplaceInstrument(state, g.selected, path, result)) {
                g.status = result; logMessage = result;
            } else {
                g.status = result; logMessage = "Instrument replacement failed: " + result;
            }
        }
    }
    if (!g.importedPath.empty()) ImGui::TextWrapped("WAV: %s", g.importedPath.c_str());
    if (!g.status.empty()) ImGui::TextWrapped("%s", g.status.c_str());
    ImGui::EndChild();
}
