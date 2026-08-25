#include "MusicEditor.h"

#include "EditorState.h"
#include "EditorUndo.h"
#include "SC4Core.h"
#include "CompressionCore.h"
#include "SNESCore.h"
#include "imgui.h"

#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int kOriginalSongCount = 0x23;
constexpr int kExtendedSongCount = 8;
constexpr int kSongCount = kOriginalSongCount + kExtendedSongCount;
constexpr uint16_t kAramBase = 0x3c00;
constexpr unsigned kExtendedDescriptorPc = 0xB7BC;
constexpr unsigned kExtendedPacketSize = 4916;
constexpr unsigned kExtendedPacketStride = 0x1340;

struct Note {
    int track = 0;
    int start = 0;
    int length = 1;
    int key = 60;
    int velocity = 100;
    int instrument = 0;
    bool percussion = false;
};

struct InstrumentChange {
    int track = 0;
    int tick = 0;
    int instrument = 0;
    size_t commandOffset = 0;
};

struct Song {
    std::vector<uint8_t> unpacked;
    std::vector<Note> notes;
    std::vector<InstrumentChange> instrumentChanges;
    std::vector<size_t> tempoOffsets;
    std::array<int, 8> instruments{};
    std::array<int, 8> trackVolumes = {120,120,120,120,120,120,120,120};
    std::array<std::vector<size_t>, 8> instrumentOffsets;
    std::array<std::vector<size_t>, 8> volumeOffsets;
    std::array<std::vector<int>, 8> originalInstrumentIds;
    int trackCount = 0;
    int tempo = 255;
    int loopCount = 0;
    int loopStartTick = 0;
    unsigned packetPc = 0;
    unsigned packetSize = 0;
    unsigned sequenceOffset = 0;
    int droppedImportNotes = 0;
    std::string warning;
};

struct State {
    const uint8_t* rom = nullptr;
    int songId = 0;
    int selectedNote = -1;
    std::set<int> selectedNotes;
    int selectedInstrumentChange = -1;
    float pixelsPerTick = 0.45f;
    Song song;
    Song pendingImport;
    std::vector<int> importTrackTargets;
    int pendingRemappedPrograms = 0;
    bool hasPendingImport = false;
    std::string status;
};

State g;

const char* CvInstrumentName(int instrument) {
    switch (instrument) {
    case 0: return "Simons clarinet";
    case 1: return "Bass";
    case 2: return "Toy";
    case 3: return "Piano";
    case 4: return "Boss instrument";
    case 5: return "Flute";
    case 6: return "Horn";
    case 7: return "Bright Organ";
    case 8: return "Church Organ";
    case 9: return "Dying Bat";
    case 10: return "Bright wooden drum";
    case 11: return "Wooden drum";
    case 12: return "Steel Drum";
    case 13: return "Drum";
    case 14: return "Sound FX vine";
    case 15: return "Bright synth 1";
    case 16: return "Bright synth 2";
    case 17: return "Die scream";
    case 20: return "Deep bass";
    default: return "Unknown";
    }
}

uint16_t ReadLe16(const std::vector<uint8_t>& data, size_t at) {
    return at + 1 < data.size() ? static_cast<uint16_t>(data[at] | (data[at + 1] << 8)) : 0;
}

uint32_t ReadBe32(const std::vector<uint8_t>& data, size_t at) {
    return at + 3 < data.size() ? (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) |
        (uint32_t(data[at + 2]) << 8) | data[at + 3] : 0;
}

std::string MidiDialog(HWND hwnd, bool save) {
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "MIDI file (*.mid)\0*.mid\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "mid";
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    ofn.lpstrTitle = save ? "Export CV4 Song as MIDI" : "Import MIDI into CV4 Song";
    const BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    return ok ? path : std::string();
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), {});
    return in.good() || in.eof();
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

uint32_t ReadVar(const std::vector<uint8_t>& data, size_t& at, size_t end) {
    uint32_t value = 0;
    for (int i = 0; i < 4 && at < end; ++i) {
        const uint8_t b = data[at++];
        value = (value << 7) | (b & 0x7f);
        if (!(b & 0x80)) break;
    }
    return value;
}

struct MidiTempoPoint { uint32_t tick = 0; uint32_t microsPerQuarter = 500000; };

std::vector<MidiTempoPoint> ReadMidiTempoMap(const std::vector<uint8_t>& data) {
    std::vector<MidiTempoPoint> tempos;
    if (data.size() < 14) return tempos;
    const int trackCount = (data[10] << 8) | data[11];
    size_t chunk = 8 + ReadBe32(data, 4);
    for (int track = 0; track < trackCount && chunk + 8 <= data.size(); ++track) {
        const uint32_t length = ReadBe32(data, chunk + 4);
        const size_t end = (std::min)(data.size(), chunk + 8 + length);
        size_t at = chunk + 8; uint32_t tick = 0; uint8_t running = 0;
        while (at < end) {
            tick += ReadVar(data, at, end); if (at >= end) break;
            uint8_t status = data[at++];
            if (status < 0x80) { if (!running) break; --at; status = running; }
            else if (status < 0xf0) running = status;
            if (status == 0xff) {
                if (at >= end) break;
                const uint8_t type = data[at++]; const uint32_t n = ReadVar(data, at, end);
                if (type == 0x51 && n == 3 && at + 3 <= end) {
                    const uint32_t micros = (uint32_t(data[at]) << 16) | (uint32_t(data[at + 1]) << 8) | data[at + 2];
                    if (micros) tempos.push_back({tick, micros});
                }
                at = (std::min)(end, at + n); continue;
            }
            if (status == 0xf0 || status == 0xf7) { const uint32_t n = ReadVar(data, at, end); at = (std::min)(end, at + n); continue; }
            at += ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 1 : 2;
            if (at > end) at = end;
        }
        chunk = end;
    }
    std::sort(tempos.begin(), tempos.end(), [](const MidiTempoPoint& a, const MidiTempoPoint& b) { return a.tick < b.tick; });
    return tempos;
}

int MidiTickToCvTick(uint32_t tick, uint16_t division, const std::vector<MidiTempoPoint>& tempos, int cvTempo) {
    uint32_t previousTick = 0, microsPerQuarter = 500000;
    uint64_t elapsedMicros = 0;
    for (const MidiTempoPoint& point : tempos) {
        if (point.tick > tick) break;
        elapsedMicros += uint64_t(point.tick - previousTick) * microsPerQuarter / division;
        previousTick = point.tick;
        microsPerQuarter = point.microsPerQuarter;
    }
    elapsedMicros += uint64_t(tick - previousTick) * microsPerQuarter / division;
    return static_cast<int>((elapsedMicros * cvTempo + 250000) / 500000);
}

struct CvDrumNote { int instrument; int key; };

CvDrumNote MidiDrumToCvNote(int midiKey) {
    if (midiKey <= 36) return {0x12, 55};                       // low steel drum
    if (midiKey == 37 || midiKey == 38 || midiKey == 40)
        return {0x12, 62};                                      // steel drum accent
    if (midiKey == 42 || midiKey == 44) return {0x13, 82};      // high drum
    if (midiKey == 46) return {0x13, 77};                       // open high drum
    if (midiKey >= 41 && midiKey <= 50) {
        const int tomPitch = std::clamp(81 - (midiKey - 41) * 2, 60, 81);
        return {0x13, tomPitch};                                // pitched toms
    }
    if (midiKey >= 49 && midiKey <= 57) return {0x13, 87};      // highest verified drum pitch
    return {0x13, 73};                                          // remaining GM percussion
}

int CvNoteToMidiDrum(int instrument, int key) {
    if (instrument == 0x12) return 36;
    if (instrument == 0x13) return key >= 70 ? 40 : 45;
    return 40;
}

int FitCvPitch(int key) {
    while (key > 95) key -= 12;
    while (key < 24) key += 12;
    return std::clamp(key, 24, 95);
}

int GeneralMidiToCvInstrument(int program) {
    program = std::clamp(program, 0, 127);
    if (program <= 7) return 3;       // pianos
    if (program <= 15) return 2;      // chromatic percussion
    if (program <= 19) return 7;      // organs
    if (program <= 23) return 8;
    if (program <= 31) return 6;      // guitars
    if (program <= 39) return program == 39 ? 20 : 1; // basses
    if (program <= 51) return 4;      // strings
    if (program <= 55) return 16;     // ensembles and choir
    if (program <= 63) return 6;      // brass
    if (program <= 71) return 0;      // reeds
    if (program <= 79) return 5;      // pipes
    if (program <= 87) return 15;     // synth leads
    if (program <= 95) return 16;     // synth pads
    return 2;
}

const std::set<int>& OriginalSongInstrumentSet(int songId) {
    static const std::set<int> expandedSongSet = {0,1,2,3,4,5,6,7,8,10,11,12,13,14,15,16,18,19,20};
    if (songId >= kOriginalSongCount) return expandedSongSet;
    static const std::array<std::set<int>, kSongCount> sets = {{
        {4,8}, {0,3,4,5,6,7,8,13,14,18,19}, {0,3,4,5,6,8,10,11,13,14,18,19},
        {1,4,5,8}, {1,3,4,5,8}, {0,1,3,4,5,11}, {0,1,2,3,4,5,6,8,10,11,12,13,18,19},
        {0,3,4,8,10,12,13,14,18,19}, {4,5,6,8,12}, {0,4}, {0,3,4},
        {0,1,5,7,8,11,13,14}, {0,1,2,3,4,10,11,12}, {0,1,3,4,5,6,8,11,13,14,18},
        {0,1,3,4,5,12,13,18}, {1,3,4,5,6,7,8,11,12,13,14,18},
        {0,3,4,5,6,7,8,13,18,19}, {0,3,4,5,8,12}, {0,1,3,5,10,11}, {1,4,5,8},
        {0,3,4,5,7,8,12}, {0,5,6}, {0,1}, {0,1,3,5}, {3,4,12},
        {0,1,3,4,6,8,10,11,12,13}, {4,7,8}, {0,12}, {0}, {0,3,4,5,6,8,12},
        {0,3,4,5,6,7,8,13,14,18,19}, {1,3}, {4,5}, {0,3,4,5,7,8,12}, {1,3,4,5}
    }};
    return sets[std::clamp(songId, 0, kSongCount - 1)];
}

int ClosestAvailableInstrument(int preferred, const std::set<int>& available) {
    if (available.count(preferred)) return preferred;
    static const std::array<int, 15> fallback = {15,16,7,8,4,6,3,0,5,2,1,20,14,13,12};
    for (const int id : fallback) if (available.count(id)) return id;
    return available.empty() ? 0 : *available.begin();
}

void PutBe16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8)); out.push_back(static_cast<uint8_t>(v));
}
void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24)); out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8)); out.push_back(static_cast<uint8_t>(v));
}
void PutLe16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8));
}
void PutVar(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t buf[5]; int n = 0; buf[n++] = static_cast<uint8_t>(v & 0x7f);
    while ((v >>= 7) != 0) buf[n++] = static_cast<uint8_t>(0x80 | (v & 0x7f));
    while (n) out.push_back(buf[--n]);
}

bool LoadSong(SC4Core& core, int songId, Song& song) {
    song = {};
    if (!core.rom || songId < 0 || songId >= kSongCount) return false;
    const unsigned tableSnes = core.region == 0 ? 0x81b610 : 0x81b5da;
    const unsigned tablePc = SNESCore::snes2pc(tableSnes);
    if (tablePc + songId * 2 + 2 > core.romSize) return false;
    const uint16_t descriptorLow = *reinterpret_cast<uint16_t*>(core.rom + tablePc + songId * 2);
    if (descriptorLow == 0) return false;
    const unsigned descriptorPc = SNESCore::snes2pc(0x810000u | descriptorLow);
    if (descriptorPc + 7 > core.romSize) return false;
    const uint8_t* desc = core.rom + descriptorPc;
    if (desc[0] != 1 || desc[2] != 0 || desc[3] != 0x3c) return false;
    const unsigned sourceSnes = unsigned(desc[4]) | (unsigned(desc[5]) << 8) | (unsigned(desc[6]) << 16);
    const unsigned sourcePc = SNESCore::snes2pc(sourceSnes);
    if (sourcePc + 3 > core.romSize) return false;
    const unsigned packetSize = *reinterpret_cast<uint16_t*>(core.rom + sourcePc);
    if (packetSize < 3 || sourcePc + packetSize > core.romSize) return false;

    song.unpacked.assign(0x10000, 0);
    const int unpackedSize = GFXRLE(core.rom, song.unpacked.data(), static_cast<int>(sourcePc + 2),
        static_cast<int>(packetSize), 0);
    if (unpackedSize <= 0 || unpackedSize > 0x10000) return false;
    song.unpacked.resize(static_cast<size_t>(unpackedSize));
    song.packetPc = sourcePc;
    song.packetSize = packetSize;

    const uint16_t sequenceAddress = ReadLe16(song.unpacked, 0);
    if (sequenceAddress < kAramBase || sequenceAddress >= kAramBase + song.unpacked.size()) {
        song.warning = "The packet's primary sequence pointer is not recognized.";
        return true;
    }
    song.sequenceOffset = sequenceAddress - kAramBase;
    struct TrackState { int time=0, length=1, duration=7, velocity=15, instrument=0, transpose=0; };
    std::array<TrackState,8> trackState{};
    int globalTranspose = 0;
    std::set<size_t> tempoOffsetSet;
    std::function<void(int,size_t,int)> parseTrack;
    parseTrack = [&](int track, size_t at, int depth) {
        if (depth > 1 || at >= song.unpacked.size()) return;
        TrackState& state = trackState[track];
        int steps = 0;
        while (at < song.unpacked.size() && steps++ < 50000) {
            const uint8_t op = song.unpacked[at++];
            if (op == 0x00) return;
            if (op < 0x80) {
                state.length = op;
                if (at < song.unpacked.size() && song.unpacked[at] < 0x80) {
                    const uint8_t style = song.unpacked[at++];
                    state.duration = style >> 4;
                    state.velocity = style & 0x0f;
                }
                continue;
            }
            if (op >= 0x80 && op <= 0xc7) {
                const int key = std::clamp(24 + (op - 0x80) + globalTranspose + state.transpose, 0, 127);
                const int audibleLength = (std::max)(1, state.length * (state.duration + 1) / 8);
                const int velocity = (std::max)(1, (state.velocity * 127 + 7) / 15);
                const bool percussion = state.instrument == 0x12 || state.instrument == 0x13;
                song.notes.push_back({track, state.time, audibleLength, key, velocity, state.instrument, percussion});
                state.time += state.length;
                continue;
            }
            if (op == 0xc8) { state.time += state.length; continue; }
            if (op == 0xc9) { state.time += state.length; continue; }
            if (op >= 0xca && op <= 0xdf) {
                const int audibleLength = (std::max)(1, state.length * (state.duration + 1) / 8);
                const int velocity = (std::max)(1, (state.velocity * 127 + 7) / 15);
                song.notes.push_back({track, state.time, audibleLength, 35 + (op - 0xca), velocity, state.instrument, true});
                state.time += state.length;
                continue;
            }
            if (op == 0xe0) {
                if (at >= song.unpacked.size()) return;
                auto& offsets=song.instrumentOffsets[track];
                if (std::find(offsets.begin(),offsets.end(),at)==offsets.end()) offsets.push_back(at);
                const size_t commandOffset = at;
                state.instrument = song.unpacked[at++];
                const auto existing = std::find_if(song.instrumentChanges.begin(), song.instrumentChanges.end(),
                    [&](const InstrumentChange& change) {
                        return change.track == track && change.tick == state.time && change.commandOffset == commandOffset;
                    });
                if (existing == song.instrumentChanges.end())
                    song.instrumentChanges.push_back({track, state.time, state.instrument, commandOffset});
                auto& ids = song.originalInstrumentIds[track];
                if (std::find(ids.begin(), ids.end(), state.instrument) == ids.end()) ids.push_back(state.instrument);
                if (offsets.size()==1 && offsets.front()==at-1) song.instruments[track] = state.instrument;
                continue;
            }
            if (op == 0xe7) {
                if (at >= song.unpacked.size()) return;
                tempoOffsetSet.insert(at); song.tempo = song.unpacked[at++]; continue;
            }
            if (op == 0xed) {
                if (at >= song.unpacked.size()) return;
                auto& offsets = song.volumeOffsets[track];
                offsets.push_back(at);
                const int volume = song.unpacked[at++];
                if (offsets.size() == 1) song.trackVolumes[track] = volume;
                continue;
            }
            if (op == 0xe9) { if (at >= song.unpacked.size()) return; globalTranspose = static_cast<int8_t>(song.unpacked[at++]); continue; }
            if (op == 0xea) { if (at >= song.unpacked.size()) return; state.transpose = static_cast<int8_t>(song.unpacked[at++]); continue; }
            if (op == 0xef) {
                if (at + 2 >= song.unpacked.size()) return;
                const uint16_t sub = ReadLe16(song.unpacked, at); const int repeats = song.unpacked[at+2] + 1; at += 3;
                if (sub >= kAramBase && sub-kAramBase < song.unpacked.size())
                    for (int i=0; i<(std::min)(repeats,256); ++i) parseTrack(track, sub-kAramBase, depth+1);
                continue;
            }
            static const std::map<uint8_t, int> params = {
                {0xe1,1},{0xe2,2},{0xe3,3},{0xe4,0},{0xe5,1},{0xe6,2},{0xe8,2},
                {0xeb,3},{0xec,0},{0xee,2},{0xf0,1},{0xf1,3},{0xf2,3},{0xf3,0},
                {0xf4,1},{0xf5,3},{0xf6,0},{0xf7,3},{0xf8,3},{0xf9,3},{0xfa,1},{0xfb,3}
            };
            auto it = params.find(op);
            if (it == params.end() || at + it->second > song.unpacked.size()) return;
            at += it->second;
        }
    };

    size_t playlist = 0;
    int blocks = 0;
    std::map<size_t, int> playlistBlockStarts;
    while (playlist + 1 < song.unpacked.size() && blocks++ < 256) {
        const size_t playlistEntry = playlist;
        uint16_t blockAddress = ReadLe16(song.unpacked, playlist); playlist += 2;
        if (blockAddress == 0) break;
        if (blockAddress < 0x100) {
            if (playlist + 1 >= song.unpacked.size()) break;
            const uint16_t loopAddress = ReadLe16(song.unpacked, playlist); playlist += 2;
            if (loopAddress < kAramBase || loopAddress - kAramBase >= song.unpacked.size()) break;
            song.loopCount = blockAddress;
            const auto loopStart = playlistBlockStarts.find(loopAddress - kAramBase);
            song.loopStartTick = loopStart == playlistBlockStarts.end() ? 0 : loopStart->second;
            break;
        }
        if (blockAddress < kAramBase || blockAddress-kAramBase+16 > song.unpacked.size()) break;
        const size_t block = blockAddress-kAramBase;
        int blockStart=0; for (const TrackState& s:trackState) blockStart=(std::max)(blockStart,s.time);
        playlistBlockStarts[playlistEntry] = blockStart;
        for (TrackState& s:trackState) s.time=blockStart;
        for (int track=0; track<8; ++track) {
            const uint16_t ptr=ReadLe16(song.unpacked,block+track*2);
            if (ptr>=kAramBase && ptr-kAramBase<song.unpacked.size()) {
                song.trackCount=(std::max)(song.trackCount,track+1); parseTrack(track,ptr-kAramBase,0);
            }
        }
        int blockEnd=blockStart; for (const TrackState& s:trackState) blockEnd=(std::max)(blockEnd,s.time);
        for (TrackState& s:trackState) s.time=blockEnd;
    }
    song.tempoOffsets.assign(tempoOffsetSet.begin(),tempoOffsetSet.end());
    std::sort(song.instrumentChanges.begin(), song.instrumentChanges.end(), [](const InstrumentChange& a, const InstrumentChange& b) {
        return a.tick < b.tick || (a.tick == b.tick && a.track < b.track);
    });
    for (auto& ids : song.originalInstrumentIds) std::sort(ids.begin(), ids.end());
    if (song.notes.empty()) song.warning = "No linear notes were found. This song may begin with a control-flow command.";
    return true;
}

bool ParseMidi(const std::string& path, Song& song, std::string& error) {
    song.droppedImportNotes = 0;
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.size() < 14 || std::string(reinterpret_cast<char*>(data.data()), 4) != "MThd") {
        error = "Could not read a Standard MIDI file."; return false;
    }
    const uint16_t division = static_cast<uint16_t>((data[12] << 8) | data[13]);
    const uint16_t midiFormat = static_cast<uint16_t>((data[8] << 8) | data[9]);
    if (!division || (division & 0x8000)) { error = "SMPTE-timed MIDI files are not supported."; return false; }
    const std::vector<MidiTempoPoint> tempoMap = ReadMidiTempoMap(data);
    const uint32_t initialMicros = tempoMap.empty() ? 500000 : tempoMap.front().microsPerQuarter;
    song.tempo = std::clamp(static_cast<int>((24000000u + initialMicros / 2) / initialMicros), 1, 255);
    struct Pending { int start; int velocity; int program; };
    std::array<int, 16> program{};
    std::map<int, std::vector<Pending>> active;
    std::vector<Note> imported;
    size_t chunk = 8 + ReadBe32(data, 4);
    int sourceTrack = 0;
    while (chunk + 8 <= data.size()) {
        const uint32_t length = ReadBe32(data, chunk + 4);
        const size_t end = (std::min)(data.size(), chunk + 8 + length);
        if (std::string(reinterpret_cast<char*>(data.data() + chunk), 4) != "MTrk") { chunk = end; continue; }
        size_t at = chunk + 8; uint32_t tick = 0; uint8_t running = 0;
        int directCvDrumInstrument = -1;
        while (at < end) {
            tick += ReadVar(data, at, end); if (at >= end) break;
            uint8_t status = data[at++];
            if (status < 0x80) { if (!running) break; --at; status = running; } else if (status < 0xf0) running = status;
            if (status == 0xff) {
                if (at >= end) break;
                const uint8_t type = data[at++];
                const uint32_t n = ReadVar(data, at, end);
                if (type == 0x03 && at + n <= end) {
                    const std::string name(reinterpret_cast<char*>(data.data() + at), n);
                    constexpr const char* prefix = "CV4 Drum Instrument ";
                    if (name.rfind(prefix, 0) == 0) {
                        const int id = std::atoi(name.c_str() + std::char_traits<char>::length(prefix));
                        if (id == 0x12 || id == 0x13) directCvDrumInstrument = id;
                    }
                }
                at = (std::min)(end, at + n);
                continue;
            }
            if (status == 0xf0 || status == 0xf7) { const uint32_t n = ReadVar(data, at, end); at = (std::min)(end, at + n); continue; }
            const int channel = status & 0x0f; const int kind = status & 0xf0;
            if (kind == 0xc0 || kind == 0xd0) {
                if (at >= end) break; const uint8_t a = data[at++]; if (kind == 0xc0) program[channel] = a; continue;
            }
            if (at + 1 >= end) break; const uint8_t key = data[at++], value = data[at++];
            const int id = channel * 128 + key;
            if (kind == 0x90 && value) active[id].push_back({static_cast<int>(tick), value, program[channel]});
            else if (kind == 0x80 || (kind == 0x90 && !value)) {
                auto& stack = active[id]; if (stack.empty()) continue; const Pending p = stack.front(); stack.erase(stack.begin());
                const int start = MidiTickToCvTick(static_cast<uint32_t>(p.start), division, tempoMap, song.tempo);
                const int finish = MidiTickToCvTick(tick, division, tempoMap, song.tempo);
                const int targetTrack = midiFormat == 0 ? (channel & 7) : std::clamp(sourceTrack - 1, 0, 7);
                const bool percussion = channel == 9;
                const CvDrumNote drum = MidiDrumToCvNote(key);
                const bool directCvDrum = percussion && directCvDrumInstrument >= 0x12;
                const int importedKey = directCvDrum ? FitCvPitch(key)
                    : (percussion ? drum.key : FitCvPitch(key));
                const int importedInstrument = directCvDrum ? directCvDrumInstrument
                    : (percussion ? drum.instrument : p.program);
                imported.push_back({targetTrack, start, (std::max)(1, finish - start), importedKey,
                    value ? value : p.velocity, importedInstrument, percussion});
            }
        }
        ++sourceTrack; chunk = end;
    }
    if (imported.empty()) { error = "The MIDI file contains no complete notes."; return false; }
    song.loopCount = 200;

    std::map<int, std::vector<Note>> sourceParts;
    for (const Note& note : imported) sourceParts[note.track].push_back(note);
    song.notes.clear(); song.instruments.fill(0); song.trackCount = 1;
    int nextTrack = 0;
    int splitNotes = 0;
    for (auto& [sourceTrack, notes] : sourceParts) {
        std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
            return a.start < b.start || (a.start == b.start && a.key > b.key);
        });
        std::vector<int> laneEnd;
        std::vector<std::vector<Note>> lanes;
        for (Note note : notes) {
            int lane = -1;
            for (int i = 0; i < static_cast<int>(laneEnd.size()); ++i) {
                if (laneEnd[i] <= note.start) { lane = i; break; }
            }
            if (lane < 0 && !laneEnd.empty()) {
                int closestLane = 0;
                for (int i = 1; i < static_cast<int>(laneEnd.size()); ++i)
                    if (laneEnd[i] < laneEnd[closestLane]) closestLane = i;
                const int overlap = laneEnd[closestLane] - note.start;
                if (overlap <= 6 && !lanes[closestLane].empty()) {
                    Note& previous = lanes[closestLane].back();
                    previous.length = (std::max)(1, note.start - previous.start);
                    lane = closestLane;
                }
            }
            if (lane < 0) {
                lane = static_cast<int>(laneEnd.size());
                laneEnd.push_back(0);
                lanes.emplace_back();
            }
            laneEnd[lane] = note.start + note.length;
            lanes[lane].push_back(note);
        }
        for (int lane = 0; lane < static_cast<int>(lanes.size()); ++lane) {
            for (Note note : lanes[lane]) {
                note.track = nextTrack;
                if (lane > 0) ++splitNotes;
                song.notes.push_back(note);
            }
            if (nextTrack < static_cast<int>(song.instruments.size()) && !lanes[lane].empty())
                song.instruments[nextTrack] = lanes[lane].front().instrument;
            ++nextTrack;
        }
    }

    // Repack every note into the earliest free voice. This is interval partitioning:
    // simultaneous notes remain separate, while later passages reuse idle voices and
    // retain their instruments for instrument-change commands during compilation.
    std::sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
        return a.start < b.start || (a.start == b.start && a.key > b.key);
    });
    std::vector<std::vector<Note>> packedVoices;
    std::vector<int> packedVoiceEnds;
    for (Note note : song.notes) {
        int destination = -1;
        for (int candidate = 0; candidate < static_cast<int>(packedVoices.size()); ++candidate)
            if (packedVoiceEnds[candidate] <= note.start) { destination = candidate; break; }
        if (destination < 0) {
            destination = static_cast<int>(packedVoices.size());
            packedVoices.emplace_back();
            packedVoiceEnds.push_back(0);
        }
        note.track = destination;
        packedVoices[destination].push_back(note);
        packedVoiceEnds[destination] = note.start + note.length;
    }
    const int mergedVoices = nextTrack - static_cast<int>(packedVoices.size());
    song.notes.clear();
    song.instruments.fill(0);
    for (int track = 0; track < static_cast<int>(packedVoices.size()); ++track) {
        auto& voice = packedVoices[track];
        std::sort(voice.begin(), voice.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        if (track < static_cast<int>(song.instruments.size()) && !voice.empty())
            song.instruments[track] = voice.front().instrument;
        song.notes.insert(song.notes.end(), voice.begin(), voice.end());
    }
    song.trackCount = (std::max)(1, static_cast<int>(packedVoices.size()));
    std::sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
        return a.start < b.start || (a.start == b.start && a.track < b.track);
    });
    for (int track = 0; track < song.trackCount; ++track) {
        std::vector<Note*> trackNotes;
        for (Note& note : song.notes) if (note.track == track) trackNotes.push_back(&note);
        std::sort(trackNotes.begin(), trackNotes.end(), [](const Note* a, const Note* b) { return a->start < b->start; });
        for (size_t i = 0; i + 1 < trackNotes.size(); ++i) {
            const int step = (std::max)(1, trackNotes[i + 1]->start - trackNotes[i]->start);
            const int keyedLength = (std::max)(1, step * 7 / 8);
            trackNotes[i]->length = (std::min)(trackNotes[i]->length, keyedLength);
        }
    }
    song.warning.clear();
    if (splitNotes) {
        if (!song.warning.empty()) song.warning += " ";
        song.warning += std::to_string(splitNotes) + " polyphonic MIDI note(s) were placed on stable additional SNES tracks.";
    }
    if (mergedVoices) {
        if (!song.warning.empty()) song.warning += " ";
        song.warning += std::to_string(mergedVoices) + " non-overlapping voice(s) were merged using instrument changes.";
    }
    return true;
}

bool SameMusicalNotes(const Song& a, const Song& b) {
    if (a.notes.size() != b.notes.size()) return false;
    auto canonical = [](const Song& song) {
        std::vector<std::array<int, 7>> notes;
        notes.reserve(song.notes.size());
        for (const Note& note : song.notes)
            notes.push_back({note.track, note.start, note.length, note.key, note.velocity, note.instrument, note.percussion ? 1 : 0});
        std::sort(notes.begin(), notes.end());
        return notes;
    };
    return canonical(a) == canonical(b);
}

void CompactSongTracks(Song& song) {
    std::array<bool, 8> used{};
    for (const Note& note : song.notes)
        if (note.track >= 0 && note.track < 8) used[note.track] = true;

    std::array<int, 8> remap{};
    remap.fill(-1);
    std::array<int, 8> compactInstruments{};
    int next = 0;
    for (int track = 0; track < 8; ++track) {
        if (!used[track]) continue;
        remap[track] = next;
        compactInstruments[next] = song.instruments[track];
        ++next;
    }
    for (Note& note : song.notes)
        if (note.track >= 0 && note.track < 8 && remap[note.track] >= 0) note.track = remap[note.track];
    for (InstrumentChange& change : song.instrumentChanges) {
        if (change.track >= 0 && change.track < 8 && remap[change.track] >= 0) change.track = remap[change.track];
        else change.track = -1;
    }
    song.instrumentChanges.erase(std::remove_if(song.instrumentChanges.begin(), song.instrumentChanges.end(),
        [&](const InstrumentChange& change) { return change.track < 0 || change.track >= next; }), song.instrumentChanges.end());
    song.instruments = compactInstruments;
    song.trackCount = (std::max)(1, next);
}

void RebuildInstrumentChangesFromNotes(Song& song) {
    song.instrumentChanges.clear();
    for (int track = 0; track < song.trackCount; ++track) {
        std::vector<Note> notes;
        for (const Note& note : song.notes) if (note.track == track) notes.push_back(note);
        std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        int current = -1;
        for (const Note& note : notes) {
            if (note.instrument != current) {
                song.instrumentChanges.push_back({track, note.start, note.instrument, 0});
                current = note.instrument;
            }
        }
    }
}

int RemapUnsupportedMidiInstruments(Song& imported, const Song& destination, int songId) {
    // Do not derive this from the currently edited song: after replacing a song
    // with a drum-only test that would remap every later MIDI program to drums.
    // Drum-like IDs are deliberately excluded from melodic program assignment.
    const std::set<int>& available = OriginalSongInstrumentSet(songId);

    std::set<int> assigned;
    std::map<int, int> programMap;
    for (const Note& note : imported.notes)
        if (!note.percussion && available.count(note.instrument)) assigned.insert(note.instrument);
    int remapped = 0;
    for (Note& note : imported.notes) {
        if (note.percussion) continue;
        if (available.count(note.instrument)) continue;
        const int sourceProgram = note.instrument;
        auto mapped = programMap.find(sourceProgram);
        if (mapped == programMap.end()) {
            int replacement = ClosestAvailableInstrument(GeneralMidiToCvInstrument(sourceProgram), available);
            assigned.insert(replacement);
            mapped = programMap.emplace(sourceProgram, replacement).first;
        }
        note.instrument = mapped->second;
        ++remapped;
    }
    for (int track = 0; track < (std::min)(imported.trackCount, 8); ++track) {
        const bool percussionTrack = std::any_of(imported.notes.begin(), imported.notes.end(), [&](const Note& note) {
            return note.track == track && note.percussion;
        });
        if (percussionTrack) continue;
        if (!available.count(imported.instruments[track])) {
            int replacement = destination.instruments[track];
            if (!available.count(replacement)) replacement = *available.begin();
            imported.instruments[track] = replacement;
        }
    }
    return remapped;
}

std::vector<uint8_t> CompileSequence(const Song& song) {
    const int tracks = std::clamp(song.trackCount, 1, 8);
    int songEnd = 1;
    std::array<const Note*, 8> finalNotes{};
    for (const Note& note : song.notes) {
        const int track = std::clamp(note.track, 0, 7);
        if (!finalNotes[track] || note.start > finalNotes[track]->start) finalNotes[track] = &note;
    }
    for (const Note* note : finalNotes) {
        if (!note) continue;
        const int releaseGap = (std::max)(1, note->length / 8);
        songEnd = (std::max)(songEnd, note->start + note->length + releaseGap + 1);
    }
    // N-SPC starts with a phrase playlist. A non-zero loop start emits a one-time
    // intro phrase followed by a second phrase that the playlist repeats.
    std::vector<uint8_t> out;
    const bool loops = song.loopCount > 0;
    const int loopStart = std::clamp(song.loopStartTick, 0, (std::max)(0, songEnd - 1));
    const bool hasIntro = loops && loopStart > 0;
    size_t introTable = 0;
    size_t loopTable = 0;
    if (hasIntro) {
        PutLe16(out, static_cast<uint16_t>(kAramBase + 10));
        PutLe16(out, static_cast<uint16_t>(kAramBase + 26));
        PutLe16(out, static_cast<uint16_t>(std::clamp(song.loopCount, 1, 255)));
        PutLe16(out, static_cast<uint16_t>(kAramBase + 2));
        PutLe16(out, 0);
        introTable = out.size();
        out.resize(out.size() + 16, 0);
        loopTable = out.size();
        out.resize(out.size() + 16, 0);
    } else {
        const uint16_t channelTableAddress = static_cast<uint16_t>(kAramBase + (loops ? 8 : 4));
        PutLe16(out, channelTableAddress);
        if (loops) {
            PutLe16(out, static_cast<uint16_t>(std::clamp(song.loopCount, 1, 255)));
            PutLe16(out, kAramBase);
        }
        PutLe16(out, 0);
        loopTable = out.size();
        out.resize(out.size() + 16, 0);
    }

    auto compilePhrase = [&](size_t channelTable, int segmentStart, int segmentEnd) {
      for (int track = 0; track < tracks; ++track) {
        const uint16_t ptr = static_cast<uint16_t>(kAramBase + out.size());
        out[channelTable + track * 2] = static_cast<uint8_t>(ptr);
        out[channelTable + track * 2 + 1] = static_cast<uint8_t>(ptr >> 8);
        int initialInstrument = song.instruments[track];
        for (const Note& n : song.notes)
            if (n.track == track && n.start <= segmentStart) initialInstrument = n.instrument;
        out.push_back(0xe0); out.push_back(static_cast<uint8_t>(std::clamp(initialInstrument, 0, 255)));
        int currentInstrument = initialInstrument;
        out.push_back(0xe1); out.push_back(10);
        out.push_back(0xed); out.push_back(static_cast<uint8_t>(std::clamp(song.trackVolumes[track], 0, 255)));
        out.push_back(0xe7);
        out.push_back(static_cast<uint8_t>(std::clamp(song.tempo, 1, 255)));
        std::vector<Note> notes;
        for (const Note& source : song.notes) {
            if (source.track != track) continue;
            const int clippedStart = (std::max)(source.start, segmentStart);
            const int clippedEnd = (std::min)(source.start + source.length, segmentEnd);
            if (clippedStart >= clippedEnd) continue;
            Note n = source;
            n.start = clippedStart - segmentStart;
            n.length = clippedEnd - clippedStart;
            notes.push_back(n);
        }
        std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        int time = 0;
        int currentLength = -1;
        int currentStyle = -1;
        auto setLength = [&](int length, int style) {
            length = std::clamp(length, 1, 127);
            if (length == currentLength && style == currentStyle) return;
            out.push_back(static_cast<uint8_t>(length));
            out.push_back(static_cast<uint8_t>(style));
            currentLength = length;
            currentStyle = style;
        };
        for (size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
            const Note& n = notes[noteIndex];
            int rest = (std::max)(0, n.start - time);
            while (rest) {
                const int part = (std::min)(rest, 127);
                setLength(part, 0x7f);
                out.push_back(0xc9);
                rest -= part;
                time += part;
            }
            if (n.instrument != currentInstrument) {
                out.push_back(0xe0);
                out.push_back(static_cast<uint8_t>(std::clamp(n.instrument, 0, 255)));
                currentInstrument = n.instrument;
            }
            const int nextStart = noteIndex + 1 < notes.size()
                ? notes[noteIndex + 1].start
                : n.start + n.length + (std::max)(1, n.length / 8);
            int step = (std::max)(1, nextStart - n.start);
            int sounding = std::clamp(n.length, 1, step);
            const int velocityRate = std::clamp((n.velocity * 15 + 63) / 127, 1, 15);
            const int first = (std::min)(step, 127);
            const int firstSounding = (std::min)(sounding, first);
            const int firstDurationRate = std::clamp((firstSounding * 8 + first / 2) / first - 1, 1, 7);
            setLength(first, (firstDurationRate << 4) | velocityRate);
            out.push_back(static_cast<uint8_t>(0x80 + std::clamp(n.key - 24, 0, 71)));
            step -= first;
            sounding -= firstSounding;
            time += first;
            while (step) {
                const int part = (std::min)(step, 127);
                if (sounding > 0) {
                    const int soundingPart = (std::min)(sounding, part);
                    const int durationRate = std::clamp((soundingPart * 8 + part / 2) / part - 1, 1, 7);
                    setLength(part, (durationRate << 4) | velocityRate);
                    out.push_back(0xc8);
                    sounding -= soundingPart;
                } else {
                    setLength(part, 0x7f);
                    out.push_back(0xc9);
                }
                step -= part;
                time += part;
            }
        }
        int trailingRest = (std::max)(0, segmentEnd - segmentStart - time);
        while (trailingRest) {
            const int part = (std::min)(trailingRest, 127);
            setLength(part, 0x7f);
            out.push_back(0xc9);
            trailingRest -= part;
            time += part;
        }
        out.push_back(0x00);
      }
    };
    if (hasIntro) compilePhrase(introTable, 0, loopStart);
    compilePhrase(loopTable, hasIntro ? loopStart : 0, songEnd);
    return out;
}

unsigned ExtendedSongPacketPc(int slot) {
    return slot < 4
        ? 0x3F2000 + static_cast<unsigned>(slot) * kExtendedPacketStride
        : 0x3FA000 + static_cast<unsigned>(slot - 4) * kExtendedPacketStride;
}

bool AddExpandedSong(SC4Core& core, int& addedSongId, std::string& error) {
    addedSongId = -1;
    if (!core.rom || !core.expandedROM || core.romSize < 0x400000 || core.type != 0) {
        error = "New songs require an expanded Super Castlevania IV ROM.";
        return false;
    }

    const unsigned tableSnes = core.region == 0 ? 0x81B610 : 0x81B5DA;
    const unsigned tablePc = SNESCore::snes2pc(tableSnes);
    const bool migrated = *reinterpret_cast<uint16_t*>(core.rom + tablePc) == 0xB7BC;
    if (!migrated) {
        const unsigned descriptorBytes = 3 * 7 + kExtendedSongCount * 7;
        if (kExtendedDescriptorPc + descriptorBytes > core.romSize ||
            !std::all_of(core.rom + kExtendedDescriptorPc, core.rom + kExtendedDescriptorPc + descriptorBytes,
                [](uint8_t value) { return value == 0xFF; })) {
            error = "The reserved expanded descriptor area is already in use.";
            return false;
        }
        for (int id = 0; id < 3; ++id) {
            const uint16_t oldLow = *reinterpret_cast<uint16_t*>(core.rom + tablePc + id * 2);
            const unsigned oldPc = SNESCore::snes2pc(0x810000u | oldLow);
            const unsigned newPc = kExtendedDescriptorPc + static_cast<unsigned>(id) * 7;
            std::copy_n(core.rom + oldPc, 7, core.rom + newPc);
            *reinterpret_cast<uint16_t*>(core.rom + tablePc + id * 2) =
                static_cast<uint16_t>(0x8000 + (newPc & 0x7FFF));
        }
        std::fill(core.rom + tablePc + kOriginalSongCount * 2,
            core.rom + tablePc + kSongCount * 2, 0);
    }

    int slot = -1;
    for (int candidate = 0; candidate < kExtendedSongCount; ++candidate) {
        if (*reinterpret_cast<uint16_t*>(core.rom + tablePc + (kOriginalSongCount + candidate) * 2) == 0) {
            slot = candidate;
            break;
        }
    }
    if (slot < 0) {
        error = "All eight expanded song slots (35-42) are already allocated.";
        return false;
    }

    const unsigned packetPc = ExtendedSongPacketPc(slot);
    if (packetPc + kExtendedPacketSize > core.romSize ||
        !std::all_of(core.rom + packetPc, core.rom + packetPc + kExtendedPacketSize,
            [](uint8_t value) { return value == 0xFF; })) {
        error = "The reserved expanded song packet area is already in use.";
        return false;
    }

    Song initial;
    initial.trackCount = 1;
    initial.tempo = 48;
    initial.loopCount = 200;
    initial.instruments[0] = 3;
    const std::vector<uint8_t> sequence = CompileSequence(initial);
    if (sequence.size() > kExtendedPacketSize - 3) {
        error = "The initial song does not fit in its 4,913-byte sequence area.";
        return false;
    }
    *reinterpret_cast<uint16_t*>(core.rom + packetPc) = kExtendedPacketSize;
    core.rom[packetPc + 2] = 0x80;
    std::copy(sequence.begin(), sequence.end(), core.rom + packetPc + 3);
    std::fill(core.rom + packetPc + 3 + sequence.size(), core.rom + packetPc + kExtendedPacketSize, 0);

    const unsigned descriptorPc = kExtendedDescriptorPc + 3 * 7 + static_cast<unsigned>(slot) * 7;
    core.rom[descriptorPc + 0] = 1;
    core.rom[descriptorPc + 1] = 0xA0;
    core.rom[descriptorPc + 2] = 0;
    core.rom[descriptorPc + 3] = 0x3C;
    const unsigned packetSnes = SNESCore::pc2snes(packetPc);
    core.rom[descriptorPc + 4] = static_cast<uint8_t>(packetSnes);
    core.rom[descriptorPc + 5] = static_cast<uint8_t>(packetSnes >> 8);
    core.rom[descriptorPc + 6] = static_cast<uint8_t>(packetSnes >> 16);

    addedSongId = kOriginalSongCount + slot;
    *reinterpret_cast<uint16_t*>(core.rom + tablePc + addedSongId * 2) =
        static_cast<uint16_t>(0x8000 + (descriptorPc & 0x7FFF));
    return true;
}

bool ApplySong(SC4Core& core, Song& song, std::string& error) {
    if (!core.expandedROM || song.packetPc < 0x100000) { error = "Expand the ROM before replacing music."; return false; }
    if (song.packetPc + 3 > core.romSize || core.rom[song.packetPc + 2] != 0x80) {
        error = "This music packet is not in the editable expanded format."; return false;
    }
    const std::vector<uint8_t> sequence = CompileSequence(song);
    const size_t needed = sequence.size();
    // Expanded packets are stored as one literal control byte followed by their unpacked bytes.
    const size_t capacity = song.packetSize >= 3 ? song.packetSize - 3 : 0;
    if (needed > capacity) {
        error = "Compiled song needs " + std::to_string(needed) + " bytes; this packet reserves " + std::to_string(capacity) + "."; return false;
    }
    std::copy(sequence.begin(), sequence.end(), core.rom + song.packetPc + 3);
    std::fill(core.rom + song.packetPc + 3 + needed, core.rom + song.packetPc + song.packetSize, 0);
    return true;
}

bool ApplyTempo(SC4Core& core, Song& song, std::string& error) {
    if (!core.expandedROM || song.packetPc < 0x100000 || core.rom[song.packetPc + 2] != 0x80) {
        error = "Expand the ROM before changing music."; return false;
    }
    if (song.tempoOffsets.empty()) { error = "No tempo command was found in this song."; return false; }
    const uint8_t tempo = static_cast<uint8_t>(std::clamp(song.tempo, 1, 255));
    for (const size_t offset : song.tempoOffsets) {
        if (offset >= song.packetSize - 3) { error = "A tempo command lies outside this packet."; return false; }
        core.rom[song.packetPc + 3 + offset] = tempo;
        if (offset < song.unpacked.size()) song.unpacked[offset] = tempo;
    }
    return true;
}

bool ApplyInstruments(SC4Core& core, Song& song, std::string& error) {
    if (!core.expandedROM || song.packetPc < 0x100000 || core.rom[song.packetPc + 2] != 0x80) {
        error = "Expand the ROM before changing music."; return false;
    }
    int patched = 0;
    for (int track=0; track<song.trackCount; ++track) {
        const uint8_t instrument=static_cast<uint8_t>(std::clamp(song.instruments[track],0,255));
        for (const size_t offset:song.instrumentOffsets[track]) {
            if (offset>=song.packetSize-3 || offset>=song.unpacked.size()) {
                error="An instrument command lies outside this packet."; return false;
            }
            core.rom[song.packetPc+3+offset]=instrument; song.unpacked[offset]=instrument; ++patched;
        }
    }
    if (!patched) { error="No initial instrument commands were found."; return false; }
    return true;
}

bool ApplyTrackVolumes(SC4Core& core, Song& song, std::string& error) {
    if (!core.expandedROM || song.packetPc < 0x100000 || core.rom[song.packetPc + 2] != 0x80) {
        error = "Expand the ROM before changing music.";
        return false;
    }
    int patched = 0;
    for (int track = 0; track < song.trackCount; ++track) {
        const uint8_t volume = static_cast<uint8_t>(std::clamp(song.trackVolumes[track], 0, 255));
        for (const size_t offset : song.volumeOffsets[track]) {
            if (offset >= song.packetSize - 3 || offset >= song.unpacked.size()) {
                error = "A track-volume command lies outside this packet.";
                return false;
            }
            core.rom[song.packetPc + 3 + offset] = volume;
            song.unpacked[offset] = volume;
            ++patched;
        }
    }
    if (!patched) {
        error = "No track-volume commands were found.";
        return false;
    }
    return true;
}

bool ExportMidi(const Song& song, const std::string& path) {
    std::vector<uint8_t> track;
    const uint32_t microsPerQuarter = static_cast<uint32_t>(24000000 / (std::max)(1, song.tempo));
    track.insert(track.end(), {0,0xff,0x51,0x03,static_cast<uint8_t>(microsPerQuarter>>16),
        static_cast<uint8_t>(microsPerQuarter>>8),static_cast<uint8_t>(microsPerQuarter)});
    struct E { int tick; int type; int key; int velocity; int channel; int instrument; bool percussion; };
    std::vector<E> events;
    for (const Note& n : song.notes) {
        const int channel = n.percussion ? 9 : n.track;
        const int midiKey = n.percussion ? CvNoteToMidiDrum(n.instrument, n.key) : n.key;
        events.push_back({n.start,2,midiKey,n.velocity,channel,n.instrument,n.percussion});
        events.push_back({n.start+n.length,0,midiKey,0,channel,n.instrument,n.percussion});
    }
    std::sort(events.begin(), events.end(), [](const E& a, const E& b) { return a.tick < b.tick || (a.tick == b.tick && a.type < b.type); });
    int time = 0;
    std::array<int,16> currentProgram; currentProgram.fill(-1);
    for (const E& e : events) {
        PutVar(track, static_cast<uint32_t>((std::max)(0, e.tick-time))); time=e.tick;
        if (e.type==2 && !e.percussion && currentProgram[e.channel&15]!=e.instrument) {
            track.push_back(static_cast<uint8_t>(0xc0|(e.channel&15))); track.push_back(static_cast<uint8_t>(e.instrument&0x7f));
            currentProgram[e.channel&15]=e.instrument; PutVar(track,0);
        }
        track.push_back(static_cast<uint8_t>((e.type==0?0x80:0x90)|(e.channel&15)));
        track.push_back(static_cast<uint8_t>(e.key)); track.push_back(static_cast<uint8_t>(e.velocity));
    }
    track.insert(track.end(), {0,0xff,0x2f,0});
    std::vector<uint8_t> out = {'M','T','h','d'}; PutBe32(out, 6); PutBe16(out, 0); PutBe16(out, 1); PutBe16(out, 48);
    out.insert(out.end(), {'M','T','r','k'}); PutBe32(out, static_cast<uint32_t>(track.size())); out.insert(out.end(), track.begin(), track.end());
    return WriteFile(path, out);
}

void ApplyInstrumentChangesToNotes(Song& song) {
    for (Note& note : song.notes) {
        int active = song.instruments[std::clamp(note.track, 0, 7)];
        int activeTick = -1;
        for (const InstrumentChange& change : song.instrumentChanges) {
            if (change.track == note.track && change.tick <= note.start && change.tick >= activeTick) {
                active = change.instrument;
                activeTick = change.tick;
            }
        }
        note.instrument = active;
    }
}

void DrawPianoRoll(Song& song, int& selected, std::set<int>& selectedNotes, int& selectedChange, float scale) {
    int endTick = 192;
    for (const Note& n : song.notes) endTick = (std::max)(endTick, n.start + n.length);
    endTick = (std::max)(endTick, song.loopStartTick + 48);
    const float row = 6.0f, markerRow = 18.0f;
    const float markerHeight = (std::max)(1, song.trackCount) * markerRow + 6.0f;
    const float width = (std::max)(600.0f, endTick * scale);
    ImGui::BeginChild("music-roll", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("music-canvas", ImVec2(width, markerHeight + 96 * row));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    static bool boxSelecting = false;
    static ImVec2 boxStart;
    for (int track = 0; track < song.trackCount; ++track) {
        const float y = origin.y + track * markerRow;
        dl->AddText(ImVec2(origin.x + 3, y + 2), IM_COL32(180, 180, 185, 255), ("T" + std::to_string(track + 1)).c_str());
        dl->AddLine(ImVec2(origin.x, y + markerRow - 1), ImVec2(origin.x + width, y + markerRow - 1), IM_COL32(48,48,53,255));
    }
    for (int key = 0; key <= 96; key += 12) dl->AddLine(ImVec2(origin.x, origin.y + markerHeight + (96-key)*row), ImVec2(origin.x+width, origin.y+markerHeight+(96-key)*row), IM_COL32(55,55,60,255));
    for (int tick = 0; tick <= endTick; tick += 48) dl->AddLine(ImVec2(origin.x+tick*scale,origin.y), ImVec2(origin.x+tick*scale,origin.y+markerHeight+96*row), IM_COL32(55,55,60,255));
    if (song.loopCount > 0 && song.loopStartTick > 0) {
        const float x = origin.x + song.loopStartTick * scale;
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + markerHeight + 96 * row), IM_COL32(255,220,50,255), 2.0f);
        dl->AddText(ImVec2(x + 4.0f, origin.y + 2.0f), IM_COL32(255,220,50,255), "LOOP");
    }
    static const ImU32 colors[8] = {IM_COL32(70,150,240,255),IM_COL32(240,95,90,255),IM_COL32(80,190,115,255),IM_COL32(235,190,65,255),IM_COL32(185,105,230,255),IM_COL32(65,195,200,255),IM_COL32(240,125,55,255),IM_COL32(180,180,190,255)};
    for (int i = 0; i < static_cast<int>(song.instrumentChanges.size()); ++i) {
        const InstrumentChange& change = song.instrumentChanges[i];
        const float x = origin.x + change.tick * scale;
        const float y = origin.y + change.track * markerRow + 1;
        const std::string label = "I " + std::to_string(change.instrument);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 a(x, y), b(x + size.x + 8.0f, y + markerRow - 3.0f);
        dl->AddRectFilled(a, b, colors[change.track & 7]);
        dl->AddText(ImVec2(a.x + 4, a.y + 1), IM_COL32(15,15,18,255), label.c_str());
        if (i == selectedChange) dl->AddRect(a, b, IM_COL32(255,220,50,255), 0.0f, ImDrawFlags_None, 2.0f);
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(a,b)) { selectedChange = i; selected = -1; selectedNotes.clear(); }
    }
    for (int i = 0; i < static_cast<int>(song.notes.size()); ++i) {
        const Note& n = song.notes[i]; ImVec2 a(origin.x+n.start*scale, origin.y+markerHeight+(95-n.key)*row); ImVec2 b(a.x+(std::max)(2.0f,n.length*scale),a.y+row-1);
        dl->AddRectFilled(a,b,colors[n.track&7]);
        if (i==selected || selectedNotes.count(i)) dl->AddRect(a,b,IM_COL32(255,220,50,255),0.0f,ImDrawFlags_None,2.0f);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        int hit = -1;
        for (int i = static_cast<int>(song.notes.size()) - 1; i >= 0; --i) {
            const Note& n = song.notes[i];
            const ImVec2 a(origin.x+n.start*scale, origin.y+markerHeight+(95-n.key)*row);
            const ImVec2 b(a.x+(std::max)(2.0f,n.length*scale),a.y+row-1);
            if (ImGui::IsMouseHoveringRect(a,b)) { hit = i; break; }
        }
        if (hit >= 0) {
            if (ImGui::GetIO().KeyCtrl) {
                if (selectedNotes.count(hit)) selectedNotes.erase(hit); else selectedNotes.insert(hit);
            } else {
                selectedNotes.clear(); selectedNotes.insert(hit);
            }
            selected = hit; selectedChange = -1;
        } else if (mouse.y >= origin.y + markerHeight) {
            boxSelecting = true; boxStart = mouse;
            if (!ImGui::GetIO().KeyCtrl) selectedNotes.clear();
            selected = -1; selectedChange = -1;
        }
    }
    if (boxSelecting) {
        const ImVec2 a((std::min)(boxStart.x, mouse.x), (std::min)(boxStart.y, mouse.y));
        const ImVec2 b((std::max)(boxStart.x, mouse.x), (std::max)(boxStart.y, mouse.y));
        dl->AddRectFilled(a, b, IM_COL32(255,220,50,30));
        dl->AddRect(a, b, IM_COL32(255,220,50,220));
        if (ImGui::IsMouseReleased(0)) {
            for (int i = 0; i < static_cast<int>(song.notes.size()); ++i) {
                const Note& n = song.notes[i];
                const ImVec2 na(origin.x+n.start*scale, origin.y+markerHeight+(95-n.key)*row);
                const ImVec2 nb(na.x+(std::max)(2.0f,n.length*scale),na.y+row-1);
                if (na.x <= b.x && nb.x >= a.x && na.y <= b.y && nb.y >= a.y) selectedNotes.insert(i);
            }
            boxSelecting = false;
        }
    }
    ImGui::EndChild();
}

} // namespace

void ResetMusicEditor() { g = {}; }

void DrawMusicEditor(EditorState& state, HWND hwnd, std::string& logMessage) {
    if (!state.session.IsLoaded()) { ImGui::TextDisabled("Load a ROM to edit music."); return; }
    SC4Core& core = state.session.Core();
    if (g.rom != core.rom) { g = {}; g.rom = core.rom; LoadSong(core, g.songId, g.song); }
    ImGui::SetNextItemWidth(90);
    int songId = g.songId;
    if (ImGui::InputInt("Song ID", &songId)) {
        g.songId = std::clamp(songId, 0, kSongCount-1); g.selectedNote = -1; g.selectedNotes.clear(); g.selectedInstrumentChange = -1;
        if (!LoadSong(core, g.songId, g.song)) g.status = "Could not decode this song packet.";
    }
    ImGui::SameLine(); ImGui::Text("%d track(s), %zu notes, packet %u bytes", g.song.trackCount, g.song.notes.size(), g.song.packetSize);
    ImGui::SameLine();
    if (ImGui::Button("Add Song")) {
        RomUndoSnapshot beforeAdd = state.session.CreateUndoSnapshot(state.selectedEventIndex);
        int addedSongId = -1;
        std::string error;
        if (AddExpandedSong(core, addedSongId, error)) {
            state.session.BeginEdit();
            CommitUndoSnapshot(state, std::move(beforeAdd));
            g.songId = addedSongId;
            g.selectedNote = -1;
            g.selectedNotes.clear();
            g.selectedInstrumentChange = -1;
            LoadSong(core, g.songId, g.song);
            g.status = "Added Song ID " + std::to_string(addedSongId) + " with 4,913 bytes available.";
            logMessage = g.status;
        } else {
            g.status = error;
            logMessage = "Add song failed: " + error;
        }
    }
    if (ImGui::Button("Import MIDI")) {
        const std::string path = MidiDialog(hwnd, false);
        if (!path.empty()) {
            Song imported = g.song; std::string error;
            if (ParseMidi(path, imported, error)) {
                g.pendingRemappedPrograms = RemapUnsupportedMidiInstruments(imported, g.song, g.songId);
                g.pendingImport = std::move(imported);
                g.hasPendingImport = true;
                g.importTrackTargets.resize(g.pendingImport.trackCount);
                for (int track = 0; track < g.pendingImport.trackCount; ++track)
                    g.importTrackTargets[track] = track < 8 ? track + 1 : 0;
                ImGui::OpenPopup("MIDI Track Mapping");
            } else { g.status = error; logMessage = "Music import failed: " + error; }
        }
    }
    if (g.hasPendingImport) ImGui::OpenPopup("MIDI Track Mapping");
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("MIDI Track Mapping", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Choose where each imported voice will go.");
        ImGui::Separator();
        static const ImVec4 trackColors[8] = {
            ImVec4(70/255.f,150/255.f,240/255.f,1), ImVec4(240/255.f,95/255.f,90/255.f,1),
            ImVec4(80/255.f,190/255.f,115/255.f,1), ImVec4(235/255.f,190/255.f,65/255.f,1),
            ImVec4(185/255.f,105/255.f,230/255.f,1), ImVec4(65/255.f,195/255.f,200/255.f,1),
            ImVec4(240/255.f,125/255.f,55/255.f,1), ImVec4(180/255.f,180/255.f,190/255.f,1)
        };
        const char* destinations[] = {"Skip", "Track 1", "Track 2", "Track 3", "Track 4", "Track 5", "Track 6", "Track 7", "Track 8"};
        ImGui::BeginChild("##midi_voices", ImVec2(0, 360), ImGuiChildFlags_Borders);
        for (int track = 0; track < g.pendingImport.trackCount; ++track) {
            int noteCount = 0;
            int drumCount = 0;
            std::set<int> instruments;
            for (const Note& note : g.pendingImport.notes) if (note.track == track) {
                ++noteCount;
                if (note.percussion) ++drumCount; else instruments.insert(note.instrument);
            }
            ImGui::PushID(track);
            ImGui::ColorButton("##color", trackColors[track % 8], ImGuiColorEditFlags_NoTooltip, ImVec2(16,16));
            ImGui::SameLine(); ImGui::Text("Imported track %d", track + 1);
            ImGui::SameLine(155); ImGui::TextDisabled("%d notes", noteCount);
            ImGui::SameLine(245); ImGui::SetNextItemWidth(125);
            ImGui::Combo("##destination", &g.importTrackTargets[track], destinations, IM_ARRAYSIZE(destinations));
            ImGui::SameLine();
            std::string ids;
            for (int id : instruments) { if (!ids.empty()) ids += ", "; ids += std::to_string(id); }
            if (drumCount == noteCount) ImGui::TextDisabled("Percussion");
            else if (drumCount) ImGui::TextDisabled("IDs %s + drums", ids.empty() ? "none" : ids.c_str());
            else ImGui::TextDisabled("IDs %s", ids.empty() ? "none" : ids.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        std::array<int,9> destinationUse{};
        for (int track=0; track<g.pendingImport.trackCount; ++track) ++destinationUse[g.importTrackTargets[track]];
        bool merged = false;
        for (int destination=1; destination<=8; ++destination) if (destinationUse[destination] > 1) merged = true;
        if (merged) ImGui::TextColored(ImVec4(1,.75f,.25f,1), "Merged tracks may contain overlapping notes; one SNES voice cannot play a chord.");
        ImGui::Separator();
        if (ImGui::Button("Import", ImVec2(100,0))) {
            Song imported = g.pendingImport;
            int mappingDroppedNotes = 0;
            imported.notes.erase(std::remove_if(imported.notes.begin(), imported.notes.end(), [&](Note& note) {
                const int destination = note.track >= 0 && note.track < static_cast<int>(g.importTrackTargets.size())
                    ? g.importTrackTargets[note.track] : 0;
                if (!destination) { ++mappingDroppedNotes; return true; }
                note.track = destination - 1;
                return false;
            }), imported.notes.end());
            const int droppedNotes = imported.droppedImportNotes + mappingDroppedNotes;
            imported.trackCount = 1;
            for (const Note& note : imported.notes) imported.trackCount = (std::max)(imported.trackCount, note.track + 1);
            imported.instruments.fill(0);
            std::array<int,8> firstTick; firstTick.fill(0x7fffffff);
            for (const Note& note : imported.notes) {
                if (note.start < firstTick[note.track]) {
                    firstTick[note.track] = note.start;
                    imported.instruments[note.track] = note.instrument;
                }
            }
            RebuildInstrumentChangesFromNotes(imported);
            std::string error;
            RomUndoSnapshot beforeImport = state.session.CreateUndoSnapshot(state.selectedEventIndex);
            if (SameMusicalNotes(g.song, imported)) {
                g.status = "MIDI matches the exported song exactly. ROM music was left unchanged. " +
                    std::to_string(droppedNotes) + " notes dropped during import.";
            } else if (ApplySong(core, imported, error)) {
                state.session.BeginEdit(); CommitUndoSnapshot(state, std::move(beforeImport));
                LoadSong(core, g.songId, g.song);
                g.selectedNote = -1; g.selectedNotes.clear(); g.selectedInstrumentChange = -1;
                g.status = "MIDI imported with the selected track mapping. " +
                    std::to_string(droppedNotes) + " notes dropped during import. Save ROM to keep it.";
                if (g.pendingRemappedPrograms)
                    g.status += " " + std::to_string(g.pendingRemappedPrograms) + " note program(s) were mapped to CV4 instruments.";
            } else g.status = "Music import failed: " + error;
            logMessage = g.status;
            g.hasPendingImport = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0))) {
            g.hasPendingImport = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export MIDI")) {
        const std::string path = MidiDialog(hwnd, true);
        if (!path.empty()) { g.status = ExportMidi(g.song,path) ? "MIDI exported: "+path : "Could not write MIDI file."; logMessage = g.status; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Song")) { LoadSong(core,g.songId,g.song); g.selectedNote=-1; g.selectedNotes.clear(); g.selectedInstrumentChange=-1; g.status="Reloaded song from ROM."; }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild Notes to ROM")) {
        std::string error;
        RomUndoSnapshot beforeApply = state.session.CreateUndoSnapshot(state.selectedEventIndex);
        for (Note& n : g.song.notes) {
            n.track = std::clamp(n.track, 0, 7); n.start = (std::max)(0, n.start);
            n.length = (std::max)(1, n.length); n.key = std::clamp(n.key, 0, 95);
            n.velocity = std::clamp(n.velocity, 1, 127);
        }
        if (ApplySong(core, g.song, error)) {
            state.session.BeginEdit(); CommitUndoSnapshot(state, std::move(beforeApply));
            LoadSong(core, g.songId, g.song);
            g.selectedNote = -1; g.selectedNotes.clear(); g.selectedInstrumentChange = -1;
            g.status = "Music notes applied. Save ROM to keep them."; logMessage = g.status;
        } else { g.status = error; logMessage = "Music apply failed: " + error; }
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::SliderFloat("Zoom", &g.pixelsPerTick, 0.15f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("Tempo", &g.song.tempo)) g.song.tempo = std::clamp(g.song.tempo, 1, 255);
    ImGui::SameLine();
    if (ImGui::Button("Apply Tempo")) {
        std::string error;
        RomUndoSnapshot beforeTempo = state.session.CreateUndoSnapshot(state.selectedEventIndex);
        if (ApplyTempo(core, g.song, error)) {
            state.session.BeginEdit(); CommitUndoSnapshot(state, std::move(beforeTempo));
            LoadSong(core, g.songId, g.song);
            g.status = "Tempo applied without rebuilding the song. Save ROM to keep it."; logMessage = g.status;
        } else { g.status = error; logMessage = "Tempo apply failed: " + error; }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("Loop start", &g.song.loopStartTick))
        g.song.loopStartTick = (std::max)(0, g.song.loopStartTick);
    if (g.selectedNote >= 0 && g.selectedNote < static_cast<int>(g.song.notes.size())) {
        ImGui::SameLine();
        if (ImGui::Button("Set from selected note"))
            g.song.loopStartTick = g.song.notes[g.selectedNote].start;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Loop")) {
        std::string error;
        const int requestedLoopStart = g.song.loopStartTick;
        RomUndoSnapshot beforeLoop = state.session.CreateUndoSnapshot(state.selectedEventIndex);
        if (ApplySong(core, g.song, error)) {
            state.session.BeginEdit();
            CommitUndoSnapshot(state, std::move(beforeLoop));
            LoadSong(core, g.songId, g.song);
            if (g.song.loopStartTick == requestedLoopStart) {
                g.status = "Loop start " + std::to_string(g.song.loopStartTick) + " applied. Save ROM to keep it.";
            } else {
                g.status = "Loop was rebuilt, but reloaded as " + std::to_string(g.song.loopStartTick) +
                    " instead of " + std::to_string(requestedLoopStart) + ".";
            }
            logMessage = g.status;
        } else {
            g.status = "Loop apply failed: " + error;
            logMessage = g.status;
        }
    }
    ImGui::TextUnformatted("Replace every change on track:");
    static const ImVec4 instrumentTrackColors[8] = {
        {70.0f / 255.0f, 150.0f / 255.0f, 240.0f / 255.0f, 1.0f},
        {240.0f / 255.0f, 95.0f / 255.0f, 90.0f / 255.0f, 1.0f},
        {80.0f / 255.0f, 190.0f / 255.0f, 115.0f / 255.0f, 1.0f},
        {235.0f / 255.0f, 190.0f / 255.0f, 65.0f / 255.0f, 1.0f},
        {185.0f / 255.0f, 105.0f / 255.0f, 230.0f / 255.0f, 1.0f},
        {65.0f / 255.0f, 195.0f / 255.0f, 200.0f / 255.0f, 1.0f},
        {240.0f / 255.0f, 125.0f / 255.0f, 55.0f / 255.0f, 1.0f},
        {180.0f / 255.0f, 180.0f / 255.0f, 190.0f / 255.0f, 1.0f},
    };
    for (int track = 0; track < g.song.trackCount; ++track) {
        if (track) ImGui::SameLine();
        ImGui::PushID(track);
        ImGui::BeginGroup();
        ImGui::ColorButton("##track_color", instrumentTrackColors[track],
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
            ImVec2(54.0f, 4.0f));
        ImGui::SetNextItemWidth(54.0f);
        ImGui::InputInt("##instrument", &g.song.instruments[track], 0, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Track %d: %d - %s\nReplace every instrument change on this track",
            track + 1, g.song.instruments[track], CvInstrumentName(g.song.instruments[track]));
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::SameLine();
    if (ImGui::Button("Replace All on Tracks")) {
        std::string error;
        RomUndoSnapshot beforeInstruments=state.session.CreateUndoSnapshot(state.selectedEventIndex);
        if (ApplyInstruments(core,g.song,error)) {
            state.session.BeginEdit(); CommitUndoSnapshot(state,std::move(beforeInstruments));
            LoadSong(core, g.songId, g.song);
            g.selectedNote = -1; g.selectedNotes.clear(); g.selectedInstrumentChange = -1;
            g.status="Track instruments applied without rebuilding the song. Save ROM to keep them."; logMessage=g.status;
        } else { g.status=error; logMessage="Instrument apply failed: "+error; }
    }
    for (int track = 0; track < g.song.trackCount; ++track) {
        if (track) ImGui::SameLine();
        ImGui::TextColored(instrumentTrackColors[track], "T%d: %s", track + 1,
            CvInstrumentName(g.song.instruments[track]));
    }
    ImGui::TextUnformatted("Track volume:");
    for (int track = 0; track < g.song.trackCount; ++track) {
        if (track) ImGui::SameLine();
        ImGui::PushID(1000 + track);
        ImGui::BeginGroup();
        ImGui::TextColored(instrumentTrackColors[track], "T%d", track + 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.0f);
        ImGui::SliderInt("##volume", &g.song.trackVolumes[track], 0, 255, "%d",
            ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Track %d volume: %d", track + 1, g.song.trackVolumes[track]);
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Track Volumes")) {
        std::string error;
        RomUndoSnapshot beforeVolumes = state.session.CreateUndoSnapshot(state.selectedEventIndex);
        if (ApplyTrackVolumes(core, g.song, error)) {
            state.session.BeginEdit();
            CommitUndoSnapshot(state, std::move(beforeVolumes));
            LoadSong(core, g.songId, g.song);
            g.status = "Track volumes applied. Save ROM to keep them.";
            logMessage = g.status;
        } else {
            g.status = "Track volume apply failed: " + error;
            logMessage = g.status;
        }
    }
    for (int track = 0; track < g.song.trackCount; ++track) {
        std::string ids;
        for (const int id : g.song.originalInstrumentIds[track]) {
            if (!ids.empty()) ids += ", ";
            ids += std::to_string(id) + " " + CvInstrumentName(id);
        }
        ImGui::Text("Track %d original IDs: %s", track + 1, ids.empty() ? "none" : ids.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Instrument IDs confirmed in this track when the song was loaded");
    }
    if (!g.status.empty()) ImGui::TextWrapped("%s",g.status.c_str());
    if (g.song.loopCount > 0)
        ImGui::TextColored(ImVec4(1,.75f,.25f,1), "Loop count: %d, start: %d. The repeating section is shown once.",
            g.song.loopCount, g.song.loopStartTick);
    if (!g.song.warning.empty()) ImGui::TextColored(ImVec4(1,.75f,.25f,1),"%s",g.song.warning.c_str());
    DrawPianoRoll(g.song,g.selectedNote,g.selectedNotes,g.selectedInstrumentChange,g.pixelsPerTick);
    if (!g.selectedNotes.empty()) {
        ImGui::Separator();
        ImGui::Text("%zu notes selected", g.selectedNotes.size());
        ImGui::SameLine();
        static int selectionTrack = 1;
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("Track##note-selection", &selectionTrack);
        selectionTrack = std::clamp(selectionTrack, 1, 8);
        ImGui::SameLine();
        if (ImGui::Button("Move Selection")) {
            for (const int index : g.selectedNotes)
                if (index >= 0 && index < static_cast<int>(g.song.notes.size())) g.song.notes[index].track = selectionTrack - 1;
            g.song.trackCount = (std::max)(g.song.trackCount, selectionTrack);
            RebuildInstrumentChangesFromNotes(g.song);
            g.status = std::to_string(g.selectedNotes.size()) + " notes moved to track " + std::to_string(selectionTrack) + ". Rebuild Notes to ROM to apply.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Octave Down")) {
            for (const int index : g.selectedNotes)
                if (index >= 0 && index < static_cast<int>(g.song.notes.size()))
                    g.song.notes[index].key = std::clamp(g.song.notes[index].key - 12, 24, 95);
            g.status = std::to_string(g.selectedNotes.size()) + " notes moved down one octave. Rebuild Notes to ROM to apply.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Octave Up")) {
            for (const int index : g.selectedNotes)
                if (index >= 0 && index < static_cast<int>(g.song.notes.size()))
                    g.song.notes[index].key = std::clamp(g.song.notes[index].key + 12, 24, 95);
            g.status = std::to_string(g.selectedNotes.size()) + " notes moved up one octave. Rebuild Notes to ROM to apply.";
        }
    }
    if (g.selectedInstrumentChange >= 0 && g.selectedInstrumentChange < static_cast<int>(g.song.instrumentChanges.size())) {
        InstrumentChange& change = g.song.instrumentChanges[g.selectedInstrumentChange];
        ImGui::Separator();
        ImGui::Text("Selected instrument change");
        ImGui::SetNextItemWidth(90);
        int instrument = change.instrument;
        if (ImGui::InputInt("Instrument", &instrument)) {
            change.instrument = std::clamp(instrument, 0, 255);
            ApplyInstrumentChangesToNotes(g.song);
        }
        ImGui::SameLine();
        ImGui::Text("Track %d, tick %d", change.track + 1, change.tick);
        ImGui::SameLine();
        if (ImGui::Button("Delete Change")) {
            g.song.instrumentChanges.erase(g.song.instrumentChanges.begin() + g.selectedInstrumentChange);
            g.selectedInstrumentChange = -1;
            ApplyInstrumentChangesToNotes(g.song);
        }
    }
    if (g.selectedNote >= 0 && g.selectedNote < static_cast<int>(g.song.notes.size())) {
        Note& n=g.song.notes[g.selectedNote]; ImGui::Separator(); ImGui::Text("Selected note");
        ImGui::SetNextItemWidth(90); ImGui::InputInt("Track",&n.track); ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("Start",&n.start);
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("Length",&n.length); ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("Note",&n.key);
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("Velocity",&n.velocity);
        ImGui::SameLine(); ImGui::Checkbox("Percussion", &n.percussion);
        ImGui::SameLine();
        if (ImGui::Button("Delete Note")) { g.song.notes.erase(g.song.notes.begin()+g.selectedNote); g.selectedNote=-1; g.selectedNotes.clear(); }
    }
    if (ImGui::Button("Add Instrument Change")) {
        InstrumentChange change;
        if (g.selectedNote >= 0 && g.selectedNote < static_cast<int>(g.song.notes.size())) {
            const Note& note = g.song.notes[g.selectedNote];
            change.track = std::clamp(note.track, 0, 7);
            change.tick = (std::max)(0, note.start);
            change.instrument = note.instrument;
        } else {
            change.instrument = g.song.instruments[0];
        }
        g.song.instrumentChanges.push_back(change);
        std::sort(g.song.instrumentChanges.begin(), g.song.instrumentChanges.end(), [](const InstrumentChange& a, const InstrumentChange& b) {
            return a.tick < b.tick || (a.tick == b.tick && a.track < b.track);
        });
        const auto selected = std::find_if(g.song.instrumentChanges.begin(), g.song.instrumentChanges.end(), [&](const InstrumentChange& candidate) {
            return candidate.track == change.track && candidate.tick == change.tick && candidate.instrument == change.instrument;
        });
        g.selectedInstrumentChange = selected == g.song.instrumentChanges.end() ? -1 : static_cast<int>(selected - g.song.instrumentChanges.begin());
        g.selectedNote = -1;
        ApplyInstrumentChangesToNotes(g.song);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Note")) {
        g.song.notes.push_back({0,0,48,60,100,g.song.instruments[0]}); g.selectedNotes.clear(); g.selectedNote=static_cast<int>(g.song.notes.size())-1;
    }
}
