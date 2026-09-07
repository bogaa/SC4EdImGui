#include "SpriteEditor.h"

#include "EditorState.h"
#include "EditorUndo.h"
#include "EventNames.h"
#include "ImageClipboard.h"
#include "imgui.h"
#include "SC4Core.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

    enum class SpriteTileSource {
        Vram,
        SimonWram,
        FontCache,
    };
    
    struct SpritePiece {
        int x = 0;
        int y = 0;
        SpriteTileSource source = SpriteTileSource::Vram;
        unsigned tile = 0;
        unsigned palette = 0;
        bool hflip = false;
        bool vflip = false;
    };
    
    struct SpriteEntry {
        uint32_t key = 0;
        std::string label;
        std::vector<std::string> sources;
        WORD assemblyOffset = 0;
        WORD slotOffset = 0;
        RECT bounds = {};
        std::vector<SpritePiece> pieces;
        bool denseTileGrid = false;
    };

    static uint32_t g_selectedSpriteKey = 0xFFFFFFFFu;
    static uint32_t g_selectedFrameGroupKey = 0xFFFFFFFFu;
    static int g_selectedFrameIndex = 0;
    static int g_paintValue = 1;
    static int g_canvasZoom = 8;
    static int g_spritePaletteId = 0;
    static uint32_t g_spritePaletteFrameKey = 0xFFFFFFFFu;
    static std::string g_spriteClipboardStatus;

    static bool CanReadRom(const SC4Core& core, unsigned pcOffset, unsigned bytes)
    {
        return core.rom && pcOffset <= core.romSize && bytes <= core.romSize - pcOffset;
    }

    static bool RawTileHasPixels(const BYTE* raw)
    {
        for (int i = 0; i < 0x40; ++i) {
            if (raw[i]) {
                return true;
            }
        }
        return false;
    }

    static unsigned ReadByteAt(const SC4Core& core, unsigned snesAddress)
    {
        const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(snesAddress));
        return CanReadRom(core, pcOffset, 1) ? core.rom[pcOffset] : 0;
    }

    static unsigned ReadWordAt(const SC4Core& core, unsigned snesAddress)
    {
        const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(snesAddress));
        return CanReadRom(core, pcOffset, 2) ? *reinterpret_cast<const WORD*>(core.rom + pcOffset) : 0;
    }

    static unsigned EventAssemblyOffset(const EventInfo& event)
    {
        switch (event.eventId & 0xFF) {
        case 0x01: return 0x9061;
        case 0x02: return 0x900C;
        case 0x03: return 0xA4F8;
        case 0x04: return 0xA693;
        case 0x07: return 0x916B;
        case 0x08: return 0x89D1;
        case 0x09: return 0x8BEA;
        case 0x0A: return 0x8CFA;
        case 0x0B: return 0x8D70;
        case 0x0C: return 0x8D61;
        case 0x0D: return 0x8F2E;
        case 0x0E: return 0x80D2;
        case 0x0F: return 0x8EAB;
        case 0x10: return 0x8EB9;
        case 0x11:
        case 0x12: return 0x8AF4;
        case 0x14: return 0xA619;
        case 0x16: return 0xA682;
        case 0x17: return 0xA6EF;
        case 0x2A: return 0xAD7D;
        case 0x2C: return 0x99EE;
        case 0x30: return 0x8DEF;
        case 0x31: return 0x93BE;
        case 0x32: return 0x8A41;
        case 0x33: return 0x8B66;
        case 0x34: return 0x8D89;
        case 0x35: return 0x9A8C;
        case 0x36: return 0x9AC7;
        case 0x37: return 0xA5A5;
        case 0x3A: return 0xA597;
        case 0x3B: return 0xA54A;
        case 0x3C: return 0x8AA3;
        case 0x3E: return 0x8976;
        case 0x3F: return 0xA5AA;
        case 0x40: return 0xA54A;
        case 0x42: return 0x90D7;
        case 0x43: return 0x97BB;
        case 0x44: return 0xA5D1;
        case 0x4A: return 0xA754;
        case 0x4B: return 0x8A6F;
        case 0x4C: return 0x8C58;
        case 0x4D: return 0xA7BC;
        case 0x4E: return 0x918E;
        case 0x51: return 0x8C6D;
        case 0x52: return 0x8F5C;
        case 0x53: return 0x814F;
        case 0x54: return 0x9226;
        case 0x56: return 0x919D;
        case 0x57: return 0x9387;
        case 0x58: return 0x92FB;
        case 0x59: return 0x9390;
        case 0x5A: return 0x8E41;
        case 0x5B: return 0x8E6B;
        case 0x5C: return 0x9A6A;
        case 0x5D: return 0x95A1;
        case 0x5E: return 0x94C7;
        case 0x60: return 0xA50C;
        case 0x61: return 0xA85B;
        case 0x62: return 0xA84A;
        case 0x64: return 0xA869;
        case 0x69: return 0x9470;
        case 0x6B: return 0x8D50;
        case 0x6C: return 0xA87F;
        case 0x6D: return 0x965A;
        case 0x6E: return 0x96BE;
        case 0x6F: return 0x9648;
        case 0x71: return 0x962E;
        case 0x70: return 0x968B;
        case 0x72: return 0x9717;
        case 0x73: return 0x96E4;
        case 0x74: return 0x9730;
        case 0x75: return 0x973F;
        case 0x76: return 0x984C;
        case 0x78: return 0x8DCC;
        case 0x79: return 0x8DDD;
        case 0x7B: return 0x8B66;
        case 0x7E: return 0x95EB;
        case 0x7F: return 0x9B59;
        default: return 0;
        }
    
        if (event.eventId == 0x06) {
            switch (event.eventSubId & 0xFF) {
            case 0x00: return 0xA74B;
            case 0x01: return 0xA7F0;
            case 0x02: return 0xA7F9;
            case 0x03: return 0xA5A5;
            default: return 0;
            }
        }
        if (event.eventId == 0x2E) {
            switch (event.eventSubId & 0xFF) {
            case 0x00: return 0xA507;
            case 0x01:
            case 0x02: return 0xE1E4;
            default: return 0;
            }
        }
        if (event.eventId == 0x68) {
            const unsigned subId = event.eventSubId & 0xFF;
            if (subId >= 0x40 && subId <= 0x43) {
                return 0xA89A;
            }
            if (subId >= 0xC0 && subId <= 0xC6) {
                return 0xA884;
            }
        }
        if (event.eventId == 0x7C) {
            const unsigned subId = event.eventSubId & 0xFF;
            if (subId >= 0x80 && subId <= 0x82) {
                return 0xE47C;
            }
        }
        return 0;
    }

    static std::vector<WORD> KnownCv4AssemblyOffsets(const SC4Core& core)
    {
        std::vector<WORD> offsets = {
            0x80D2, 0x814F, 0x8976, 0x89D1, 0x8A41, 0x8A6F, 0x8AA3, 0x8AF4, 0x8B66,
            0x8BEA, 0x8C58, 0x8C6D, 0x8CFA, 0x8D50, 0x8D61, 0x8D70, 0x8D89, 0x8DCC,
            0x8DDD, 0x8DEF, 0x8E41, 0x8E6B, 0x8EAB, 0x8EB9, 0x8F2E, 0x8F5C, 0x900C,
            0x9061, 0x90D7, 0x916B, 0x918E, 0x919D, 0x9226, 0x92FB, 0x9387, 0x9390,
            0x93BE, 0x9470, 0x94C7, 0x95A1, 0x95EB, 0x962E, 0x9648, 0x965A, 0x968B,
            0x96BE, 0x96E4, 0x9717, 0x9730, 0x973F, 0x97BB, 0x984C, 0x99EE, 0x9A6A,
            0x9A8C, 0x9AC7, 0x9B59, 0xA4F8, 0xA507, 0xA50C, 0xA54A, 0xA597, 0xA5A5,
            0xA5AA, 0xA5D1, 0xA682, 0xA693, 0xA6EF, 0xA74B, 0xA754, 0xA7BC, 0xA7F0,
            0xA7F9, 0xA84A, 0xA85B, 0xA869, 0xA87F, 0xA884, 0xA89A, 0xAD7D, 0xE1E4,
            0xE47C
        };
    
        if (core.rom) {
            const WORD candleBase = static_cast<WORD>(ReadWordAt(core, 0x81A654));
            if (candleBase) {
                offsets.push_back(candleBase);
            }
            for (int i = 0; i < 0x13; ++i) {
                const WORD dropOffset = static_cast<WORD>(ReadWordAt(core, 0x81A654 + (i << 1)));
                if (dropOffset) {
                    offsets.push_back(dropOffset);
                }
            }
        }
    
        std::sort(offsets.begin(), offsets.end());
        offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
        return offsets;
    }

    static WORD NextKnownCv4AssemblyOffset(const SC4Core& core, WORD assemblyOffset)
    {
        for (WORD offset : KnownCv4AssemblyOffsets(core)) {
            if (offset > assemblyOffset) {
                return offset;
            }
        }
        return 0xFFFF;
    }

    static WORD SpriteFrameEnd(const SC4Core& core, WORD assemblyOffset)
    {
        const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(0x840000 | assemblyOffset));
        if (!CanReadRom(core, pcOffset, 1)) {
            return 0;
        }
    
        const unsigned tileCount = core.rom[pcOffset];
        if (tileCount == 0 || tileCount > 0x40 || !CanReadRom(core, pcOffset, 1 + tileCount * 4)) {
            return 0;
        }
        return static_cast<WORD>(assemblyOffset + 1 + tileCount * 4);
    }

    static bool TryGetSpriteSlotOffset(const SC4Core& core, const EventInfo& event, unsigned& slotOffset)
    {
        slotOffset = 0;
        if (event.eventId == 0 || event.type == EVENT_TYPE_CANDLE) {
            return true;
        }

    //    slotOffset = 0;       // autospawner needs to use a spriteID present in the level.. the piller might use a hardcoded slot.. need to check again and make a list.
    //    if (event.eventId == 0x38) {
    //        return true;
    //    }

        const unsigned gfxOffset = ReadWordAt(core, 0x81A900 + 3 * (event.eventId & 0xFF));
        const unsigned levelOffset = ReadWordAt(core, 0x868BCD + 2 * core.level);
        const unsigned spriteLoadPc = SNESCore::snes2pc(static_cast<int>(0x860000 + levelOffset));
        if (!gfxOffset || !CanReadRom(core, spriteLoadPc, 1)) {
            return false;
        }
    
        const BYTE* spriteLoad = core.rom + spriteLoadPc;
        const unsigned count = *spriteLoad++;
        unsigned slotNum = 0;
        for (unsigned i = 0; i < count && CanReadRom(core, static_cast<unsigned>(spriteLoad - core.rom), 1); ++i) {
            const unsigned index = *spriteLoad++;
            const unsigned spriteCount = ReadByteAt(core, 0x81AA80 + index);
            const unsigned currentGfxOffset = ReadWordAt(core, 0x81A900 + 3 * index);
            if (gfxOffset == currentGfxOffset) {
                slotOffset = ReadWordAt(core, 0x819534 + 0x13A0 + 2 * slotNum);
                return true;
            }
            slotNum += spriteCount;
        }
    
        return false;
    }

    static unsigned GetSpriteVramCacheBase(const SC4Core& core)
    {
        unsigned vramByteAddr = 0x4000;
        if (core.type == 0) {
            vramByteAddr = 0xC000;
        } else if (core.type == 1) {
            vramByteAddr = 0x0000;
        }
        return vramByteAddr * 2;
    }

    static BYTE GetSpritePixel(const SC4Core& core, unsigned tile, int x, int y)
    {
        if (tile >= 0x400 || x < 0 || x >= 8 || y < 0 || y >= 8) {
            return 0;
        }
    
        const unsigned base = GetSpriteVramCacheBase(core);
        const BYTE* image = core.vramCache + base + (tile << 6);
        return image[x + y * 8] & 0xF;
    }

    static bool GetSimonTileRaw(const SC4Core& core, unsigned tile, BYTE raw[0x40])
    {
        const unsigned ramOffset = 0x10000 + tile * 0x20;
        if (ramOffset + 0x20 <= sizeof(core.ram)) {
            const_cast<SC4Core&>(core).tile4bpp2raw(const_cast<BYTE*>(core.ram + ramOffset), raw);
            if (RawTileHasPixels(raw)) {
                return true;
            }
        }
    
        if (!core.rom || !core.expandedROM) {
            return false;
        }
    
        const unsigned wramAddr = 0x7F0000 + tile * 0x20;
        std::vector<unsigned> groups;
        groups.push_back(core.level);
        if (core.level != core.numLevels) {
            groups.push_back(core.numLevels);
        }
    
        for (unsigned group : groups) {
            const auto groupIt = core.expandedOffset.find(group);
            if (groupIt == core.expandedOffset.end()) {
                continue;
            }
    
            for (const auto& entry : groupIt->second) {
                const unsigned expandedDstAddr = entry.first;
                const unsigned expandedRomOffset = entry.second.first;
                const unsigned expandedSize = entry.second.second;
                if (wramAddr >= expandedDstAddr && wramAddr + 0x20 <= expandedDstAddr + expandedSize &&
                    CanReadRom(core, expandedRomOffset + (wramAddr - expandedDstAddr), 0x20)) {
                    const_cast<SC4Core&>(core).tile4bpp2raw(core.rom + expandedRomOffset + (wramAddr - expandedDstAddr), raw);
                    return RawTileHasPixels(raw);
                }
            }
        }
    
        return false;
    }

    static BYTE GetSpritePixel(const SC4Core& core, SpriteTileSource source, unsigned tile, int x, int y)
    {
        if (x < 0 || x >= 8 || y < 0 || y >= 8) {
            return 0;
        }
        if (source == SpriteTileSource::SimonWram) {
            BYTE raw[0x40] = {};
            return GetSimonTileRaw(core, tile, raw) ? (raw[x + y * 8] & 0xF) : 0;
        }
        if (source == SpriteTileSource::FontCache) {
            if (tile >= 0x100) {
                return 0;
            }
            return core.fontCache[0x400 + (tile << 6) + x + y * 8] & 0x3;
        }
        return GetSpritePixel(core, tile, x, y);
    }

    static bool HasSpriteTilePixels(const SC4Core& core, SpriteTileSource source, unsigned tile)
    {
        if (source == SpriteTileSource::SimonWram) {
            BYTE raw[0x40] = {};
            return GetSimonTileRaw(core, tile, raw);
        }
        if (source == SpriteTileSource::FontCache) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    if (GetSpritePixel(core, source, tile, x, y)) {
                        return true;
                    }
                }
            }
            return false;
        }
        if (tile >= 0x400) {
            return false;
        }
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (GetSpritePixel(core, tile, x, y)) {
                    return true;
                }
            }
        }
        return false;
    }

    static void SetSpritePixel(SC4Core& core, SpriteTileSource source, unsigned tile, int x, int y, BYTE value)
    {
        if (x < 0 || x >= 8 || y < 0 || y >= 8) {
            return;
        }
    
        if (source == SpriteTileSource::FontCache) {
            return;
        }
    
        if (source == SpriteTileSource::SimonWram) {
            const unsigned ramOffset = 0x10000 + tile * 0x20;
            if (ramOffset + 0x20 > sizeof(core.ram)) {
                return;
            }
            BYTE raw[0x40] = {};
            GetSimonTileRaw(core, tile, raw);
            raw[x + y * 8] = value & 0xF;
            core.raw2tile4bpp(raw, core.ram + ramOffset);
            core.simonSpriteUpdate.insert(tile);
            return;
        }
    
        if (tile >= 0x400) {
            return;
        }
        const unsigned base = GetSpriteVramCacheBase(core);
        BYTE* image = core.vramCache + base + (tile << 6);
        image[x + y * 8] = value & 0xF;
        std::memcpy(core.spriteCache + (tile << 6), image, 0x40);
        core.raw2tile4bpp(image, core.vram + (base >> 1) + (tile << 5));
        core.spriteUpdate.insert(tile);
    }

    static void SetSpriteFramePalette(EditorState& state, const SpriteEntry& sprite, unsigned palette)
    {
        SC4Core& core = state.session.Core();
        if (!sprite.assemblyOffset || !core.rom) {
            return;
        }
    
        const unsigned pcOffset = SNESCore::snes2pc(static_cast<int>(0x840000 | sprite.assemblyOffset));
        if (!CanReadRom(core, pcOffset, 1)) {
            return;
        }
    
        const unsigned tileCount = core.rom[pcOffset];
        if (tileCount > 0x80 || !CanReadRom(core, pcOffset + 1, tileCount * 4)) {
            return;
        }
    
        BYTE* tileBase = core.rom + pcOffset + 1;
        for (unsigned i = 0; i < tileCount; ++i) {
            tileBase += 2;
            WORD* mapPtr = reinterpret_cast<WORD*>(tileBase);
            WORD map = *mapPtr;
            tileBase += 2;
    
            const unsigned tile = (map & 0xFF) + sprite.slotOffset;
            const unsigned info = (map >> 8) & 0xFF;
            const bool largeSprite = (info & 0x20) != 0;
            const bool hflip = ((info >> 6) & 0x1) != 0;
            const bool vflip = ((info >> 7) & 0x1) != 0;
            bool usedBySprite = false;
    
            for (unsigned j = 0; j < (largeSprite ? 4u : 1u); ++j) {
                const unsigned tileOffset = largeSprite ? (j ^ (hflip ? 0x1u : 0x0u) ^ (vflip ? 0x2u : 0x0u)) : j;
                const unsigned actualTile = tile + (tileOffset % 2) + (tileOffset / 2) * 16;
                if (std::any_of(sprite.pieces.begin(), sprite.pieces.end(), [actualTile](const SpritePiece& piece) { return piece.tile == actualTile; })) {
                    usedBySprite = true;
                    break;
                }
            }
    
            if (usedBySprite) {
                const WORD newInfo = static_cast<WORD>((info & ~0x0Eu) | ((palette & 0x7u) << 1));
                const WORD newMap = static_cast<WORD>((map & 0x00FFu) | (newInfo << 8));
                const unsigned mapPcOffset = static_cast<unsigned>(reinterpret_cast<BYTE*>(mapPtr) - core.rom);
                state.session.WriteRomPc(mapPcOffset, 2, newMap);
            }
        }
    }

    static bool BuildSprite(const SC4Core& core, WORD assemblyOffset, WORD slotOffset, SpriteEntry& entry)
    {
        if (!assemblyOffset || !core.rom) {
            return false;
        }
    
        entry.assemblyOffset = assemblyOffset;
        entry.slotOffset = slotOffset;
        entry.pieces.clear();
        entry.bounds = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
    
        const BYTE* tileBase = core.rom + SNESCore::snes2pc(0x840000 | assemblyOffset);
        const unsigned tileCnt = *tileBase++;
        for (unsigned i = 0; i < tileCnt; ++i) {
            const char xRel = static_cast<char>(*tileBase++);
            const char yRel = static_cast<char>(*tileBase++);
            const WORD map = *reinterpret_cast<const WORD*>(tileBase);
            tileBase += 2;
    
            const unsigned tile = (map & 0xFF) + slotOffset;
            const unsigned info = (map >> 8) & 0xFF;
            const bool largeSprite = (info & 0x20) != 0;
            const bool hflip = ((info >> 6) & 0x1) != 0;
            const bool vflip = ((info >> 7) & 0x1) != 0;
            const unsigned pal = (info >> 1) & 0x7;
    
            for (unsigned j = 0; j < (largeSprite ? 4u : 1u); ++j) {
                const int xposOffset = static_cast<int>(j % 2) * 8;
                const int yposOffset = static_cast<int>(j / 2) * 8;
                const unsigned tileOffset = largeSprite ? (j ^ (hflip ? 0x1 : 0x00) ^ (vflip ? 0x2 : 0x00)) : j;
                const unsigned actualTile = tile + (tileOffset % 2) + (tileOffset / 2) * 16;
                if (!HasSpriteTilePixels(core, SpriteTileSource::Vram, actualTile)) {
                    continue;
                }
    
                SpritePiece piece = {};
                piece.x = static_cast<int>(xRel) + xposOffset;
                piece.y = static_cast<int>(yRel) + yposOffset;
                piece.tile = actualTile;
                piece.palette = pal;
                piece.hflip = hflip;
                piece.vflip = vflip;
                entry.pieces.push_back(piece);
                entry.bounds.left = (std::min)(entry.bounds.left, LONG(piece.x));
                entry.bounds.top = (std::min)(entry.bounds.top, LONG(piece.y));
                entry.bounds.right = (std::max)(entry.bounds.right, LONG(piece.x + 8));
                entry.bounds.bottom = (std::max)(entry.bounds.bottom, LONG(piece.y + 8));
            }
        }
    
        return !entry.pieces.empty() && entry.bounds.left < entry.bounds.right && entry.bounds.top < entry.bounds.bottom;
    }

    static void AddAssemblySpriteEntry(std::vector<SpriteEntry>& sprites, std::set<uint32_t>& seen, const SC4Core& core, WORD assemblyOffset, WORD slotOffset, const std::string& label, const std::string& source)
    {
        if (!assemblyOffset) {
            return;
        }
    
        const uint32_t key = (static_cast<uint32_t>(assemblyOffset) << 16) | slotOffset;
        auto existing = std::find_if(sprites.begin(), sprites.end(), [key](const SpriteEntry& entry) {
            return entry.key == key;
        });
        if (existing != sprites.end()) {
            if (std::find(existing->sources.begin(), existing->sources.end(), source) == existing->sources.end()) {
                existing->sources.push_back(source);
                existing->label += " | " + label;
            }
            return;
        }
    
        SpriteEntry entry = {};
        entry.key = key;
        entry.label = label;
        entry.sources.push_back(source);
        if (BuildSprite(core, assemblyOffset, slotOffset, entry)) {
            entry.label = label;
            sprites.push_back(std::move(entry));
            seen.insert(key);
        }
    }

    static void AddTileGridSprite(std::vector<SpriteEntry>& sprites, const SC4Core& core, uint32_t key, const std::string& label, SpriteTileSource source, unsigned firstTile, unsigned tileColumns, unsigned tileRows, unsigned sheetColumns, unsigned palette)
    {
        if (!tileColumns || !tileRows || !sheetColumns) {
            return;
        }
    
        SpriteEntry entry = {};
        entry.key = key;
        entry.label = label;
        entry.sources.push_back(label);
        entry.bounds = { 0, 0, LONG(tileColumns * 8), LONG(tileRows * 8) };
    
        for (unsigned y = 0; y < tileRows; ++y) {
            for (unsigned x = 0; x < tileColumns; ++x) {
                const unsigned tile = firstTile + y * sheetColumns + x;
                if (!HasSpriteTilePixels(core, source, tile)) {
                    continue;
                }
    
                SpritePiece piece = {};
                piece.x = static_cast<int>(x * 8);
                piece.y = static_cast<int>(y * 8);
                piece.source = source;
                piece.tile = tile;
                piece.palette = palette & 0xF;
                entry.pieces.push_back(piece);
            }
        }
    
        if (!entry.pieces.empty()) {
            sprites.push_back(std::move(entry));
        }
    }

    static void AddSimonComponentSprites(std::vector<SpriteEntry>& sprites, const SC4Core& core)
    {
        constexpr unsigned sheetColumns = 16;
        constexpr unsigned sheetTiles = 0x800;
        constexpr unsigned sheetRows = sheetTiles / sheetColumns;
    
        struct Component {
            std::vector<unsigned> tiles;
            unsigned minX = sheetColumns;
            unsigned minY = sheetRows;
            unsigned maxX = 0;
            unsigned maxY = 0;
        };
    
        std::vector<bool> occupied(sheetTiles, false);
        std::vector<bool> seen(sheetTiles, false);
        for (unsigned tile = 0; tile < sheetTiles; ++tile) {
            occupied[tile] = HasSpriteTilePixels(core, SpriteTileSource::SimonWram, tile);
        }
    
        std::vector<Component> components;
        for (unsigned start = 0; start < sheetTiles; ++start) {
            if (!occupied[start] || seen[start]) {
                continue;
            }
    
            Component component;
            std::vector<unsigned> stack;
            stack.push_back(start);
            seen[start] = true;
    
            while (!stack.empty()) {
                const unsigned tile = stack.back();
                stack.pop_back();
    
                const unsigned x = tile % sheetColumns;
                const unsigned y = tile / sheetColumns;
                component.tiles.push_back(tile);
                component.minX = (std::min)(component.minX, x);
                component.minY = (std::min)(component.minY, y);
                component.maxX = (std::max)(component.maxX, x);
                component.maxY = (std::max)(component.maxY, y);
    
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) {
                            continue;
                        }
                        const int nx = static_cast<int>(x) + dx;
                        const int ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(sheetColumns) || ny >= static_cast<int>(sheetRows)) {
                            continue;
                        }
                        const unsigned next = static_cast<unsigned>(ny) * sheetColumns + static_cast<unsigned>(nx);
                        if (!occupied[next] || seen[next]) {
                            continue;
                        }
                        seen[next] = true;
                        stack.push_back(next);
                    }
                }
            }
    
            components.push_back(std::move(component));
        }
    
        std::sort(components.begin(), components.end(), [](const Component& a, const Component& b) {
            if (a.minY != b.minY) {
                return a.minY < b.minY;
            }
            return a.minX < b.minX;
        });
    
        auto addComponentRange = [&](unsigned first, unsigned count, unsigned cropBottomRows, const char* label, bool includeBlankTiles) {
            if (!count || first >= components.size()) {
                return;
            }
            count = (std::min)(count, static_cast<unsigned>(components.size() - first));
    
            unsigned minX = sheetColumns;
            unsigned minY = sheetRows;
            unsigned maxX = 0;
            unsigned maxY = 0;
            for (unsigned i = first; i < first + count; ++i) {
                minX = (std::min)(minX, components[i].minX);
                minY = (std::min)(minY, components[i].minY);
                maxX = (std::max)(maxX, components[i].maxX);
                maxY = (std::max)(maxY, components[i].maxY);
            }
            if (cropBottomRows && maxY > minY + cropBottomRows) {
                maxY -= cropBottomRows;
            }
    
            SpriteEntry entry = {};
            entry.key = 0xF1000000u | first;
            entry.label = label;
            entry.sources.push_back(label);
            entry.bounds = { 0, 0, LONG((maxX - minX + 1) * 8), LONG((maxY - minY + 1) * 8) };
    
            if (includeBlankTiles) {
                entry.denseTileGrid = true;
                for (unsigned y = minY; y <= maxY; ++y) {
                    for (unsigned x = minX; x <= maxX; ++x) {
                        const unsigned tile = y * sheetColumns + x;
                        SpritePiece piece = {};
                        piece.x = static_cast<int>((x - minX) * 8);
                        piece.y = static_cast<int>((y - minY) * 8);
                        piece.source = SpriteTileSource::SimonWram;
                        piece.tile = tile;
                        piece.palette = 0;
                        entry.pieces.push_back(piece);
                    }
                }
            } else {
                for (unsigned i = first; i < first + count; ++i) {
                    std::sort(components[i].tiles.begin(), components[i].tiles.end());
                    for (unsigned tile : components[i].tiles) {
                        const unsigned x = tile % sheetColumns;
                        const unsigned y = tile / sheetColumns;
                        if (y < minY || y > maxY) {
                            continue;
                        }
                        SpritePiece piece = {};
                        piece.x = static_cast<int>((x - minX) * 8);
                        piece.y = static_cast<int>((y - minY) * 8);
                        piece.source = SpriteTileSource::SimonWram;
                        piece.tile = tile;
                        piece.palette = 0;
                        entry.pieces.push_back(piece);
                    }
                }
            }
    
            if (!entry.pieces.empty()) {
                sprites.push_back(std::move(entry));
            }
        };
    
        const unsigned simonComponentCount = (std::min)(16u, static_cast<unsigned>(components.size()));
        addComponentRange(0, simonComponentCount, 14, "Simon", true);
        for (unsigned i = simonComponentCount; i < components.size(); ++i) {
            char label[48] = {};
            std::snprintf(label, sizeof(label), "Simon component %u", i);
            addComponentRange(i, 1, 0, label, false);
        }
    }

    static std::string MakeSourceLabel(const SC4Core& core, const EventInfo& event, bool activeLoad)
    {
        char label[128] = {};
        if (activeLoad) {
            std::snprintf(label, sizeof(label), "Active %u %s", event.eventId & 0xFF, EventDisplayName(core, event));
        } else {
            std::snprintf(label, sizeof(label), "Event %u %s", event.eventId & 0xFF, EventDisplayName(core, event));
        }
        return label;
    }

    static void AddSpriteEntry(std::vector<SpriteEntry>& sprites, std::set<uint32_t>& seen, const SC4Core& core, const EventInfo& event, bool activeLoad)
    {
        const WORD assemblyOffset = static_cast<WORD>(EventAssemblyOffset(event));
        unsigned resolvedSlotOffset = 0;
        if (!assemblyOffset) {
            return;
        }
        if (!TryGetSpriteSlotOffset(core, event, resolvedSlotOffset)) {
            return;
        }
    
        const WORD slotOffset = static_cast<WORD>(resolvedSlotOffset);
    
        const uint32_t key = (static_cast<uint32_t>(assemblyOffset) << 16) | slotOffset;
        const std::string sourceLabel = MakeSourceLabel(core, event, activeLoad);
    
        auto existing = std::find_if(sprites.begin(), sprites.end(), [key](const SpriteEntry& entry) {
            return entry.key == key;
        });
    
        if (existing == sprites.end()) {
            SpriteEntry entry = {};
            entry.key = key;
            entry.label = EventDisplayName(core, event);
            entry.sources.push_back(sourceLabel);
            entry.assemblyOffset = assemblyOffset;
            entry.slotOffset = slotOffset;
            if (BuildSprite(core, assemblyOffset, slotOffset, entry)) {
                sprites.push_back(std::move(entry));
                seen.insert(key);
            }
            return;
        }
    
        if (std::find(existing->sources.begin(), existing->sources.end(), sourceLabel) == existing->sources.end()) {
            existing->sources.push_back(sourceLabel);
        }
    }

    static std::vector<SpriteEntry> BuildLevelSprites(SC4Core& core)
    {
        std::vector<SpriteEntry> sprites;
        if (!core.rom) {
            return sprites;
        }
    
        std::set<uint32_t> seen;
        for (const EventInfo& event : core.eventTable) {
            if (event.type != EVENT_TYPE_ENEMY && event.type != EVENT_TYPE_CANDLE && event.type != EVENT_TYPE_OBJECT) {
                continue;
            }
    
            AddSpriteEntry(sprites, seen, core, event, false);
        }
    
        std::set<WORD> activeIds;
        core.GetActiveEnemyId(activeIds);
        for (WORD id : activeIds) {
            EventInfo event = {};
            event.type = EVENT_TYPE_ENEMY;
            event.eventId = id;
            AddSpriteEntry(sprites, seen, core, event, true);
        }
    
        for (SpriteEntry& entry : sprites) {
            if (entry.sources.empty()) {
                continue;
            }
    
            std::string combined = entry.sources.front();
            for (size_t i = 1; i < entry.sources.size(); ++i) {
                combined += " | ";
                combined += entry.sources[i];
            }
            entry.label = combined;
        }
    
        return sprites;
    }

    static const char* GlobalItemName(int index)
    {
        switch (index) {
        case 0x00: return "Candle / Small Heart";
        case 0x01: return "Large Heart";
        case 0x02: return "Knife";
        case 0x03: return "Axe";
        case 0x04: return "Holy Water";
        case 0x05: return "Cross";
        case 0x06: return "Stopwatch";
        case 0x07: return "Rosary";
        case 0x08: return "Potion";
        case 0x09: return "Whip Upgrade";
        case 0x0A: return "Money";
        case 0x0B: return "Double Shot";
        case 0x0C: return "Triple Shot";
        case 0x0D: return "Small Meat";
        case 0x0E: return "Large Meat";
        case 0x0F: return "Orb";
        case 0x10: return "1-Up";
        default: return "Item";
        }
    }

    static void AddGlobalProjectile(std::vector<SpriteEntry>& sprites, std::set<uint32_t>& seen, const SC4Core& core, BYTE eventId, const char* label)
    {
        EventInfo event = {};
        event.type = EVENT_TYPE_ENEMY;
        event.eventId = eventId;
        const WORD assemblyOffset = static_cast<WORD>(EventAssemblyOffset(event));
        if (!assemblyOffset) {
            return;
        }
    
        unsigned slotOffset = 0;
        TryGetSpriteSlotOffset(core, event, slotOffset);
        AddAssemblySpriteEntry(sprites, seen, core, assemblyOffset, static_cast<WORD>(slotOffset), label, "Global projectile");
    }

    static std::vector<SpriteEntry> BuildPlayerSprites(SC4Core& core)
    {
        std::vector<SpriteEntry> sprites;
        if (!core.rom) {
            return sprites;
        }

        std::set<uint32_t> seen;
            AddSimonComponentSprites(sprites, core);
            AddTileGridSprite(sprites, core, 0xF0000001u, "Simon sheet", SpriteTileSource::SimonWram, 0x000, 16, 8, 16, 8);
            AddTileGridSprite(sprites, core, 0xF0000002u, "Simon whip frames", SpriteTileSource::SimonWram, 0x280, 16, 4, 16, 8);
            AddTileGridSprite(sprites, core, 0xF0000003u, "Loaded sprite tiles", SpriteTileSource::Vram, 0x000, 16, 16, 16, 8);

        for (int i = 0; i < 0x13; ++i) {
            const WORD offset = static_cast<WORD>(ReadWordAt(core, 0x81A654 + (i << 1)));
            if (!offset) {
                continue;
            }
        //    char label[64] = {};
        //    std::snprintf(label, sizeof(label), "%s %d", GlobalItemName(i), i);
        //    AddAssemblySpriteEntry(sprites, seen, core, offset, 0, label, "Global item");
        }

        return sprites;
    }



    static std::vector<SpriteEntry> BuildGlobalSprites(SC4Core& core)
    {
        std::vector<SpriteEntry> sprites;
        if (!core.rom) {
            return sprites;
        }
    
        std::set<uint32_t> seen;
    //    AddSimonComponentSprites(sprites, core);
    //    AddTileGridSprite(sprites, core, 0xF0000001u, "Simon sheet", SpriteTileSource::SimonWram, 0x000, 16, 8, 16, 8);
    //    AddTileGridSprite(sprites, core, 0xF0000002u, "Simon whip frames", SpriteTileSource::SimonWram, 0x280, 16, 4, 16, 8);
    //    AddTileGridSprite(sprites, core, 0xF0000003u, "Loaded sprite tiles", SpriteTileSource::Vram, 0x000, 16, 16, 16, 8);
    
        for (int i = 0; i < 0x13; ++i) {
            const WORD offset = static_cast<WORD>(ReadWordAt(core, 0x81A654 + (i << 1)));
            if (!offset) {
                continue;
            }
            char label[64] = {};
            std::snprintf(label, sizeof(label), "%s %d", GlobalItemName(i), i);
            AddAssemblySpriteEntry(sprites, seen, core, offset, 0, label, "Global item");
        }
    
        AddGlobalProjectile(sprites, seen, core, 0x12, "Skeleton bone");
        AddGlobalProjectile(sprites, seen, core, 0x01, "Projectile fire");
        AddGlobalProjectile(sprites, seen, core, 0x02, "Projectile bone");
        AddGlobalProjectile(sprites, seen, core, 0x53, "Axe knight axe");
        AddGlobalProjectile(sprites, seen, core, 0x54, "Ghost projectile");
        AddGlobalProjectile(sprites, seen, core, 0x5F, "Ectoplasm projectile");
        AddGlobalProjectile(sprites, seen, core, 0x60, "Falling dagger");
        AddGlobalProjectile(sprites, seen, core, 0x6A, "Falling stone");
        AddGlobalProjectile(sprites, seen, core, 0x78, "Small projectile 78");
        AddGlobalProjectile(sprites, seen, core, 0x79, "Small projectile 79");
    
        EventInfo skullProjectile = {};
        skullProjectile.type = EVENT_TYPE_OBJECT;
        skullProjectile.eventId = 0x68;
        skullProjectile.eventSubId = 0xC0;
        AddAssemblySpriteEntry(
            sprites,
            seen,
            core,
            static_cast<WORD>(EventAssemblyOffset(skullProjectile)),
            0,
            "Watching skull stone ball",
            "Enemy projectile");
    
        return sprites;
    }

    static std::vector<SpriteEntry> BuildSpriteFrames(const SC4Core& core, const SpriteEntry& baseSprite)
    {
        std::vector<SpriteEntry> frames;
        const WORD nextKnownOffset = NextKnownCv4AssemblyOffset(core, baseSprite.assemblyOffset);
        WORD frameOffset = baseSprite.assemblyOffset;
    
        for (unsigned frame = 0; frame < 24 && frameOffset && frameOffset < nextKnownOffset; ++frame) {
            const WORD nextFrameOffset = SpriteFrameEnd(core, frameOffset);
            if (!nextFrameOffset || nextFrameOffset <= frameOffset || nextFrameOffset > nextKnownOffset) {
                break;
            }
    
            SpriteEntry entry = {};
            entry.key = (static_cast<uint32_t>(frameOffset) << 16) | baseSprite.slotOffset;
            entry.label = baseSprite.label;
            entry.sources = baseSprite.sources;
            entry.assemblyOffset = frameOffset;
            entry.slotOffset = baseSprite.slotOffset;
            if (BuildSprite(core, frameOffset, baseSprite.slotOffset, entry)) {
                frames.push_back(std::move(entry));
            }
    
            frameOffset = nextFrameOffset;
        }
    
        if (frames.empty()) {
            frames.push_back(baseSprite);
        }
        return frames;
    }

    static ImU32 ToImColor(SC4Core& core, int palette, int index)
    {
        const uint16_t color = core.palCache[index | 0x80 | ((palette & 0x7) << 4)];
        const ImU8 r = static_cast<ImU8>(((color >> 10) & 0x1F) * 255 / 31);
        const ImU8 g = static_cast<ImU8>(((color >> 5) & 0x1F) * 255 / 31);
        const ImU8 b = static_cast<ImU8>((color & 0x1F) * 255 / 31);
        return IM_COL32(r, g, b, 0xFF);
    }

    static ImU32 ToPaletteRowColor(SC4Core& core, int paletteRow, int index)
    {
        const uint16_t color = core.palCache[index | ((paletteRow & 0xF) << 4)];
        const ImU8 r = static_cast<ImU8>(((color >> 10) & 0x1F) * 255 / 31);
        const ImU8 g = static_cast<ImU8>(((color >> 5) & 0x1F) * 255 / 31);
        const ImU8 b = static_cast<ImU8>((color & 0x1F) * 255 / 31);
        return IM_COL32(r, g, b, 0xFF);
    }

    static ImU32 ToFontColor(const SC4Core& core, int palette, int index)
    {
        const uint16_t color = core.fontPalCache[((palette & 0x1) << 4) | (index & 0x3)];
        const ImU8 r = static_cast<ImU8>(((color >> 10) & 0x1F) * 255 / 31);
        const ImU8 g = static_cast<ImU8>(((color >> 5) & 0x1F) * 255 / 31);
        const ImU8 b = static_cast<ImU8>((color & 0x1F) * 255 / 31);
        return IM_COL32(r, g, b, 0xFF);
    }

    static bool UsesFontTiles(const SpriteEntry& sprite)
    {
        return !sprite.pieces.empty() && std::all_of(sprite.pieces.begin(), sprite.pieces.end(), [](const SpritePiece& piece) {
            return piece.source == SpriteTileSource::FontCache;
        });
    }

    static ImU32 ToPieceColor(SC4Core& core, const SpritePiece& piece, int index)
    {
        return piece.source == SpriteTileSource::FontCache
            ? ToFontColor(core, static_cast<int>(piece.palette), index)
            : ToImColor(core, static_cast<int>(piece.palette), index);
    }

    static void DrawChecker(ImDrawList* drawList, ImVec2 min, ImVec2 max, float scale)
    {
        const float checker = (std::max)(2.0f, scale);
        for (float y = min.y; y < max.y; y += checker) {
            for (float x = min.x; x < max.x; x += checker) {
                const bool light = (((int)((x - min.x) / checker) + (int)((y - min.y) / checker)) & 1) != 0;
                drawList->AddRectFilled(
                    ImVec2(x, y),
                    ImVec2((std::min)(x + checker, max.x), (std::min)(y + checker, max.y)),
                    light ? IM_COL32(42, 42, 46, 255) : IM_COL32(18, 18, 22, 255));
            }
        }
    }

    static bool PaintAtMouse(SC4Core& core, const SpriteEntry& sprite, ImVec2 canvasMin, int zoom, BYTE value, const ImVec2& mousePos)
    {
        if (sprite.pieces.empty()) {
            return false;
        }
    
        const int localX = static_cast<int>((mousePos.x - canvasMin.x) / zoom);
        const int localY = static_cast<int>((mousePos.y - canvasMin.y) / zoom);
        const int spriteX = static_cast<int>(sprite.bounds.left) + localX;
        const int spriteY = static_cast<int>(sprite.bounds.top) + localY;
    
        for (const SpritePiece& piece : sprite.pieces) {
            if (spriteX < piece.x || spriteX >= piece.x + 8 || spriteY < piece.y || spriteY >= piece.y + 8) {
                continue;
            }
    
            int tileX = spriteX - piece.x;
            int tileY = spriteY - piece.y;
            if (piece.hflip) tileX = 7 - tileX;
            if (piece.vflip) tileY = 7 - tileY;
            SetSpritePixel(core, piece.source, piece.tile, tileX, tileY, value);
            return true;
        }
    
        return false;
    }

    static BYTE SampleAtMouse(const SC4Core& core, const SpriteEntry& sprite, ImVec2 canvasMin, int zoom, const ImVec2& mousePos)
    {
        if (sprite.pieces.empty()) {
            return 0;
        }
    
        const int localX = static_cast<int>((mousePos.x - canvasMin.x) / zoom);
        const int localY = static_cast<int>((mousePos.y - canvasMin.y) / zoom);
        const int spriteX = static_cast<int>(sprite.bounds.left) + localX;
        const int spriteY = static_cast<int>(sprite.bounds.top) + localY;
    
        if (sprite.denseTileGrid && localX >= 0 && localY >= 0) {
            const int width = static_cast<int>(sprite.bounds.right - sprite.bounds.left);
            const int height = static_cast<int>(sprite.bounds.bottom - sprite.bounds.top);
            if (localX < width && localY < height) {
                const size_t columns = static_cast<size_t>((width + 7) / 8);
                const size_t index = static_cast<size_t>(localY / 8) * columns + static_cast<size_t>(localX / 8);
                if (index < sprite.pieces.size()) {
                    const SpritePiece& piece = sprite.pieces[index];
                    return GetSpritePixel(core, piece.source, piece.tile, localX & 7, localY & 7);
                }
            }
        }
    
        for (const SpritePiece& piece : sprite.pieces) {
            if (spriteX < piece.x || spriteX >= piece.x + 8 || spriteY < piece.y || spriteY >= piece.y + 8) {
                continue;
            }
    
            int tileX = spriteX - piece.x;
            int tileY = spriteY - piece.y;
            if (piece.hflip) tileX = 7 - tileX;
            if (piece.vflip) tileY = 7 - tileY;
            return GetSpritePixel(core, piece.source, piece.tile, tileX, tileY);
        }
    
        return 0;
    }

    static bool PaintSpritePixel(SC4Core& core, const SpriteEntry& sprite, int localX, int localY, BYTE value)
    {
        const int spriteX = static_cast<int>(sprite.bounds.left) + localX;
        const int spriteY = static_cast<int>(sprite.bounds.top) + localY;
        if (sprite.denseTileGrid && localX >= 0 && localY >= 0) {
            const int width = static_cast<int>(sprite.bounds.right - sprite.bounds.left);
            const int height = static_cast<int>(sprite.bounds.bottom - sprite.bounds.top);
            if (localX < width && localY < height) {
                const size_t columns = static_cast<size_t>((width + 7) / 8);
                const size_t index = static_cast<size_t>(localY / 8) * columns + static_cast<size_t>(localX / 8);
                if (index < sprite.pieces.size()) {
                    const SpritePiece& piece = sprite.pieces[index];
                    SetSpritePixel(core, piece.source, piece.tile, localX & 7, localY & 7, value);
                    return true;
                }
            }
        }
        for (const SpritePiece& piece : sprite.pieces) {
            if (spriteX < piece.x || spriteX >= piece.x + 8 || spriteY < piece.y || spriteY >= piece.y + 8) {
                continue;
            }
            int tileX = spriteX - piece.x;
            int tileY = spriteY - piece.y;
            if (piece.hflip) tileX = 7 - tileX;
            if (piece.vflip) tileY = 7 - tileY;
            SetSpritePixel(core, piece.source, piece.tile, tileX, tileY, value);
            return true;
        }
        return false;
    }

    static uint32_t SpritePaletteColor(const SC4Core& core, bool fontTiles, int palette, int index)
    {
        if (index == 0) return 0;
        const uint16_t color = fontTiles
            ? core.fontPalCache[((palette & 0x1) << 4) | (index & 0x3)]
            : core.palCache[((palette & 0xF) << 4) | (index & 0xF)];
        const uint32_t r = ((color >> 10) & 0x1F) * 255 / 31;
        const uint32_t g = ((color >> 5) & 0x1F) * 255 / 31;
        const uint32_t b = (color & 0x1F) * 255 / 31;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    static bool CopySpriteToClipboard(HWND hwnd, const SC4Core& core, const SpriteEntry& sprite, int palette)
    {
        ClipboardImage image;
        image.width = (std::max)(1, static_cast<int>(sprite.bounds.right - sprite.bounds.left));
        image.height = (std::max)(1, static_cast<int>(sprite.bounds.bottom - sprite.bounds.top));
        image.pixels.assign(static_cast<size_t>(image.width) * image.height, 0);
        const bool fontTiles = UsesFontTiles(sprite);
    
        for (const SpritePiece& piece : sprite.pieces) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int rawX = piece.hflip ? 7 - x : x;
                    const int rawY = piece.vflip ? 7 - y : y;
                    const int value = GetSpritePixel(core, piece.source, piece.tile, rawX, rawY);
                    if (!value) continue;
                    const int dstX = piece.x - sprite.bounds.left + x;
                    const int dstY = piece.y - sprite.bounds.top + y;
                    if (dstX >= 0 && dstY >= 0 && dstX < image.width && dstY < image.height) {
                        image.pixels[static_cast<size_t>(dstY) * image.width + dstX] =
                            SpritePaletteColor(core, fontTiles, palette, value);
                    }
                }
            }
        }
        return CopyImageToClipboard(hwnd, image);
    }

    static int NearestSpritePaletteColor(const SC4Core& core, int palette, uint32_t pixel)
    {
        const int r = (pixel >> 16) & 0xFF;
        const int g = (pixel >> 8) & 0xFF;
        const int b = pixel & 0xFF;
        int bestIndex = 1;
        int bestDistance = INT_MAX;
        for (int index = 1; index < 16; ++index) {
            const uint32_t candidate = SpritePaletteColor(core, false, palette, index);
            const int dr = r - static_cast<int>((candidate >> 16) & 0xFF);
            const int dg = g - static_cast<int>((candidate >> 8) & 0xFF);
            const int db = b - static_cast<int>(candidate & 0xFF);
            const int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        return bestIndex;
    }

    static bool PasteSpriteFromClipboard(HWND hwnd, EditorState& state, const SpriteEntry& sprite, int palette)
    {
        ClipboardImage image;
        if (!ReadImageFromClipboard(hwnd, image)) {
            g_spriteClipboardStatus = "Clipboard does not contain a supported image.";
            return false;
        }
        const int width = static_cast<int>(sprite.bounds.right - sprite.bounds.left);
        const int height = static_cast<int>(sprite.bounds.bottom - sprite.bounds.top);
        if (image.width != width || image.height != height) {
            char message[128] = {};
            std::snprintf(message, sizeof(message), "Paste image must be %dx%d pixels; clipboard is %dx%d.", width, height, image.width, image.height);
            g_spriteClipboardStatus = message;
            return false;
        }
    
        PushUndo(state);
        SC4Core& core = state.session.Core();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const uint32_t pixel = image.pixels[static_cast<size_t>(y) * width + x];
                const BYTE value = ((pixel >> 24) & 0xFF) < 128
                    ? 0
                    : static_cast<BYTE>(NearestSpritePaletteColor(core, palette, pixel));
                PaintSpritePixel(core, sprite, x, y, value);
            }
        }
        state.levelRenderer.Invalidate();
        g_spriteClipboardStatus = "Pasted sprite image using the selected palette.";
        return true;
    }

    static void DrawSpriteThumbnail(SC4Core& core, ImDrawList* drawList, const SpriteEntry& sprite, ImVec2 min, ImVec2 max)
    {
        const ImVec2 size(max.x - min.x, max.y - min.y);
        const float scaleX = size.x / (sprite.bounds.right - sprite.bounds.left > 0 ? float(sprite.bounds.right - sprite.bounds.left) : 1.0f);
        const float scaleY = size.y / (sprite.bounds.bottom - sprite.bounds.top > 0 ? float(sprite.bounds.bottom - sprite.bounds.top) : 1.0f);
        const float scale = (std::min)(scaleX, scaleY);
        const float drawScale = (std::max)(1.0f, (std::min)(scale, 4.0f));
    
        DrawChecker(drawList, min, max, drawScale);
    
        for (const SpritePiece& piece : sprite.pieces) {
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    const int rawX = piece.hflip ? 7 - px : px;
                    const int rawY = piece.vflip ? 7 - py : py;
                    const BYTE pixel = GetSpritePixel(core, piece.source, piece.tile, rawX, rawY);
                    if (pixel == 0) {
                        continue;
                    }
    
                    const int drawX = piece.x - sprite.bounds.left + px;
                    const int drawY = piece.y - sprite.bounds.top + py;
                    drawList->AddRectFilled(
                        ImVec2(min.x + drawX * drawScale, min.y + drawY * drawScale),
                        ImVec2(min.x + (drawX + 1) * drawScale, min.y + (drawY + 1) * drawScale),
                        ToPieceColor(core, piece, pixel));
                }
            }
        }
    
        drawList->AddRect(min, max, IM_COL32(92, 92, 102, 255));
    }

    static void DrawSpriteCanvas(EditorState& state, HWND hwnd, const SpriteEntry& sprite)
    {
        SC4Core& core = state.session.Core();
        const bool fontTiles = UsesFontTiles(sprite);
        const int spriteWidth = (std::max)(1, static_cast<int>(sprite.bounds.right - sprite.bounds.left));
        const int spriteHeight = (std::max)(1, static_cast<int>(sprite.bounds.bottom - sprite.bounds.top));
        ImGui::TextUnformatted("Zoom");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(96.0f);
        ImGui::SliderInt("##sprite-zoom", &g_canvasZoom, 1, 16, "%d");
        g_canvasZoom = std::clamp(g_canvasZoom, 1, 16);
        ImGui::SameLine();
        ImGui::TextUnformatted("Palette ID");
        ImGui::SameLine();
        if (g_spritePaletteFrameKey != sprite.key) {
            g_spritePaletteFrameKey = sprite.key;
            g_spritePaletteId = sprite.pieces.empty() ? 8 : (fontTiles
                ? static_cast<int>(sprite.pieces.front().palette & 0x1)
                : static_cast<int>(0x8 | (sprite.pieces.front().palette & 0x7)));
        }
        ImGui::SetNextItemWidth(64.0f);
        if (ImGui::InputInt("##sprite-palette-id", &g_spritePaletteId, 1, 1)) {
            g_spritePaletteId = std::clamp(g_spritePaletteId, 0, fontTiles ? 1 : 15);
            if (!fontTiles) {
                PushUndo(state);
                SetSpriteFramePalette(state, sprite, static_cast<unsigned>(g_spritePaletteId));
                state.levelRenderer.Invalidate();
            }
        }
        g_spritePaletteId = std::clamp(g_spritePaletteId, 0, fontTiles ? 1 : 15);
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            g_spriteClipboardStatus = CopySpriteToClipboard(hwnd, core, sprite, g_spritePaletteId)
                ? "Copied assembled sprite frame to the clipboard."
                : "Could not copy the sprite image.";
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(fontTiles);
        if (ImGui::Button("Paste")) {
            PasteSpriteFromClipboard(hwnd, state, sprite, g_spritePaletteId);
        }
        ImGui::EndDisabled();
        if (!g_spriteClipboardStatus.empty()) {
            ImGui::TextWrapped("%s", g_spriteClipboardStatus.c_str());
        }
        ImGui::Text("Assembly %04X  Slot %04X", sprite.assemblyOffset, sprite.slotOffset);
        if (sprite.sources.size() > 1) {
            std::string combined;
            for (size_t i = 0; i < sprite.sources.size(); ++i) {
                if (i > 0) combined += " | ";
                combined += sprite.sources[i];
            }
            ImGui::TextDisabled("%s", combined.c_str());
        } else if (!sprite.sources.empty()) {
            ImGui::TextDisabled("%s", sprite.sources.front().c_str());
        }
        ImGui::Separator();
    
        const int palette = g_spritePaletteId;
        ImGui::TextUnformatted(fontTiles ? "Colors" : "Paint");
        const int colorCount = fontTiles ? 4 : 16;
        for (int i = 0; i < colorCount; ++i) {
            if (i > 0 && (i % 8) != 0) {
                ImGui::SameLine();
            }
            const uint16_t color = fontTiles
                ? core.fontPalCache[((palette & 0x1) << 4) | i]
                : core.palCache[i | ((palette & 0xF) << 4)];
            const ImVec4 buttonColor(
                float(((color >> 10) & 0x1F) * 255 / 31) / 255.0f,
                float(((color >> 5) & 0x1F) * 255 / 31) / 255.0f,
                float((color & 0x1F) * 255 / 31) / 255.0f,
                i == 0 ? 0.85f : 1.0f);
            ImGui::PushID(i);
            if (ImGui::ColorButton("##swatch", buttonColor, ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20)) && !fontTiles) {
                g_paintValue = i;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Value %d", i);
            }
            if (!fontTiles && g_paintValue == i) {
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(),
                    ImGui::GetItemRectMax(),
                    IM_COL32(255, 216, 64, 255),
                    0.0f,
                    0,
                    2.0f);
            }
            ImGui::PopID();
        }
        if (fontTiles) {
            ImGui::TextDisabled("2bpp HUD/font graphics (view only)");
        } else {
            ImGui::Text("Value %d", g_paintValue & 0xF);
        }
        ImGui::Separator();
    
        const ImVec2 canvasSize(spriteWidth * g_canvasZoom, spriteHeight * g_canvasZoom);
        ImGui::InvisibleButton("sprite-canvas", canvasSize);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 canvasMin = ImGui::GetItemRectMin();
        const ImVec2 canvasMax = ImGui::GetItemRectMax();
        DrawChecker(drawList, canvasMin, canvasMax, static_cast<float>(g_canvasZoom));
    
        for (const SpritePiece& piece : sprite.pieces) {
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    const int rawX = piece.hflip ? 7 - px : px;
                    const int rawY = piece.vflip ? 7 - py : py;
                    const BYTE pixel = GetSpritePixel(core, piece.source, piece.tile, rawX, rawY);
                    if (pixel == 0) {
                        continue;
                    }
    
                    const int drawX = piece.x - sprite.bounds.left + px;
                    const int drawY = piece.y - sprite.bounds.top + py;
                    drawList->AddRectFilled(
                        ImVec2(canvasMin.x + drawX * g_canvasZoom, canvasMin.y + drawY * g_canvasZoom),
                        ImVec2(canvasMin.x + (drawX + 1) * g_canvasZoom, canvasMin.y + (drawY + 1) * g_canvasZoom),
                        fontTiles ? ToFontColor(core, g_spritePaletteId, pixel) : ToPaletteRowColor(core, g_spritePaletteId, pixel));
                }
            }
        }
    
        for (const SpritePiece& piece : sprite.pieces) {
            const ImU32 border = IM_COL32(96, 136, 255, 255);
            drawList->AddRect(
                ImVec2(canvasMin.x + (piece.x - sprite.bounds.left) * g_canvasZoom, canvasMin.y + (piece.y - sprite.bounds.top) * g_canvasZoom),
                ImVec2(canvasMin.x + (piece.x - sprite.bounds.left + 8) * g_canvasZoom, canvasMin.y + (piece.y - sprite.bounds.top + 8) * g_canvasZoom),
                border,
                0.0f,
                0,
                1.0f);
        }
    
        drawList->AddRect(canvasMin, canvasMax, IM_COL32(90, 90, 102, 255));
    
        if (ImGui::IsItemHovered() && !fontTiles) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                g_paintValue = SampleAtMouse(core, sprite, canvasMin, g_canvasZoom, mouse) & 0xF;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                state.spritePaintUndoActive = false;
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!state.spritePaintUndoActive) {
                    PushUndo(state);
                    state.spritePaintUndoActive = true;
                }
                if (PaintAtMouse(core, sprite, canvasMin, g_canvasZoom, static_cast<BYTE>(g_paintValue & 0xF), mouse)) {
                    state.levelRenderer.Invalidate();
                }
            }
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.spritePaintUndoActive = false;
        }
    
        if (!fontTiles) {
            ImGui::TextDisabled("Left-drag paints. Right-click samples a value.");
        }
    }

    static void DrawSpriteBrowser(EditorState& state, HWND hwnd, std::vector<SpriteEntry>& sprites, const char* title, const char* emptyMessage)
    {
        SC4Core& core = state.session.Core();
        if (sprites.empty()) {
            ImGui::TextUnformatted(emptyMessage);
            return;
        }
    
        if (g_selectedSpriteKey == 0xFFFFFFFFu || std::none_of(sprites.begin(), sprites.end(), [](const SpriteEntry& entry) { return entry.key == g_selectedSpriteKey; })) {
            g_selectedSpriteKey = sprites.front().key;
        }
    
        const float listWidth = (std::min)(360.0f, (std::max)(220.0f, ImGui::GetContentRegionAvail().x * 0.34f));
        ImGui::BeginChild("sprite-list", ImVec2(listWidth, 0.0f), true);
        ImGui::Text("%s (%d)", title, static_cast<int>(sprites.size()));
        ImGui::Separator();
        ImGui::BeginChild("sprite-list-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        for (const SpriteEntry& entry : sprites) {
            const bool selected = entry.key == g_selectedSpriteKey;
            ImGui::PushID(static_cast<int>(entry.key));
            if (ImGui::Selectable("##sprite", selected, 0, ImVec2(0.0f, 56.0f))) {
                g_selectedSpriteKey = entry.key;
            }
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const ImVec2 thumbMin(itemMin.x + 4.0f, itemMin.y + 4.0f);
            const ImVec2 thumbMax(itemMin.x + 52.0f, itemMin.y + 52.0f);
            DrawSpriteThumbnail(core, ImGui::GetWindowDrawList(), entry, thumbMin, thumbMax);
            ImGui::GetWindowDrawList()->AddText(ImVec2(itemMin.x + 60.0f, itemMin.y + 8.0f), IM_COL32(235, 235, 235, 255), entry.label.c_str());
            char meta[64];
            std::snprintf(meta, sizeof(meta), "%04X / %04X", entry.assemblyOffset, entry.slotOffset);
            ImGui::GetWindowDrawList()->AddText(ImVec2(itemMin.x + 60.0f, itemMin.y + 28.0f), IM_COL32(160, 160, 170, 255), meta);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Assembly %04X  Slot %04X", entry.assemblyOffset, entry.slotOffset);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndChild();
    
        ImGui::SameLine();
        ImGui::BeginChild("sprite-painting", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    
        const SpriteEntry* selected = nullptr;
        for (const SpriteEntry& entry : sprites) {
            if (entry.key == g_selectedSpriteKey) {
                selected = &entry;
                break;
            }
        }
    
        if (!selected) {
            selected = &sprites.front();
            g_selectedSpriteKey = selected->key;
        }
    
        if (g_selectedFrameGroupKey != selected->key) {
            g_selectedFrameGroupKey = selected->key;
            g_selectedFrameIndex = 0;
        }
    
        std::vector<SpriteEntry> frames = BuildSpriteFrames(core, *selected);
        if (g_selectedFrameIndex < 0 || g_selectedFrameIndex >= static_cast<int>(frames.size())) {
            g_selectedFrameIndex = 0;
        }
    
        ImGui::Text("Painting - %d frame%s", static_cast<int>(frames.size()), frames.size() == 1 ? "" : "s");
        ImGui::Separator();
    
        ImGui::BeginChild("sprite-frame-strip", ImVec2(0.0f, 74.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        const float frameCell = 56.0f;
        for (int i = 0; i < static_cast<int>(frames.size()); ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }
    
            ImGui::PushID(i);
            const bool frameSelected = i == g_selectedFrameIndex;
            if (ImGui::Selectable("##frame", frameSelected, 0, ImVec2(frameCell, frameCell))) {
                g_selectedFrameIndex = i;
            }
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            DrawSpriteThumbnail(core, ImGui::GetWindowDrawList(), frames[i], ImVec2(itemMin.x + 4.0f, itemMin.y + 4.0f), ImVec2(itemMax.x - 4.0f, itemMax.y - 4.0f));
            if (frameSelected) {
                ImGui::GetWindowDrawList()->AddRect(itemMin, itemMax, IM_COL32(255, 216, 64, 255), 0.0f, 0, 2.0f);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Frame %02d\nAssembly %04X  Slot %04X", i, frames[i].assemblyOffset, frames[i].slotOffset);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    
        DrawSpriteCanvas(state, hwnd, frames[g_selectedFrameIndex]);
        ImGui::EndChild();
    }

} // namespace

void DrawSpriteEditor(EditorState& state, HWND hwnd)
{
    if (!state.session.IsLoaded()) {
        ImGui::TextUnformatted("Open a ROM to browse and paint sprites.");
        return;
    }

    SC4Core& core = state.session.Core();
    if (ImGui::BeginTabBar("sprite-editor-subtabs")) {
        const int restoredSpriteTab = state.activeSpriteTab;
        const ImGuiTabItemFlags globalFlags = state.restoreSpriteTab && restoredSpriteTab == 0
            ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        const ImGuiTabItemFlags levelFlags = state.restoreSpriteTab && restoredSpriteTab == 1
            ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        const ImGuiTabItemFlags playerPlayer = state.restoreSpriteTab && restoredSpriteTab == 2
            ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        
        if (ImGui::BeginTabItem("Global", nullptr, globalFlags)) {
            if (!state.restoreSpriteTab || restoredSpriteTab == 0) {
                state.activeSpriteTab = 0;
                state.restoreSpriteTab = false;
            }
            std::vector<SpriteEntry> sprites = BuildGlobalSprites(core);
            DrawSpriteBrowser(state, hwnd, sprites, "Global sprites", "No global sprites found.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Level", nullptr, levelFlags)) {
            if (!state.restoreSpriteTab || restoredSpriteTab == 1) {
                state.activeSpriteTab = 1;
                state.restoreSpriteTab = false;
            }
            std::vector<SpriteEntry> sprites = BuildLevelSprites(core);
            DrawSpriteBrowser(state, hwnd, sprites, "Level sprites", "No sprites found for this level.");
            ImGui::EndTabItem();          
        }
        if (ImGui::BeginTabItem("Player", nullptr, playerPlayer)) {
            if (!state.restoreSpriteTab || restoredSpriteTab == 2) {
                state.activeSpriteTab = 2;
                state.restoreSpriteTab = false;
            }
            std::vector<SpriteEntry> sprites = BuildPlayerSprites(core);
            DrawSpriteBrowser(state, hwnd, sprites, "Player sprites", "No sprites found for this player.");
            ImGui::EndTabItem();
        }







        ImGui::EndTabBar();
    }
}
