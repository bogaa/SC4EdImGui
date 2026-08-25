#pragma once

#include "LevelRenderer.h"
#include "RomInfo.h"
#include "RomSession.h"

#include <vector>

struct EditorState {
    RomSession session;
    LevelRenderer levelRenderer;
    int level = 0;
    int checkpoint = 0;
    float zoom = 2.0f;
    bool showBackground = false;
    bool showCollision = false;
    bool showEvents = true;
    bool showGrid = false;
    bool showHelp = false;
    bool showInternalEmulator = true;
    bool internalEmulatorRunning = false;
    bool followInternalEmulatorCamera = true;
    bool hasInternalEmulatorCamera = false;
    int internalEmulatorCameraX = 0;
    int internalEmulatorCameraY = 0;
    bool editLevelMode = false;
    uint16_t selectedBlock = 0;
    uint16_t selectedTile = 0;
    uint16_t selectedBehaviorTile = 0;
    std::vector<uint16_t> selectedBehaviorTiles;
    int blockBrushWidth = 1;
    int blockBrushHeight = 1;
    std::vector<uint16_t> blockBrush;
    int selectedBlockCell = 0;
    unsigned tilePaletteId = 0;
    bool tileLayerPriority = false;
    unsigned drawTilesClipboardPalette = 0;
    bool tileFlipX = false;
    bool tileFlipY = false;
    int selectedEventIndex = -1;
    std::vector<RomUndoSnapshot> undoStack;
    std::vector<RomUndoSnapshot> redoStack;
    bool levelPaintUndoActive = false;
    bool spritePaintUndoActive = false;
    int activeToolTab = 0;
    int activeEventPaletteTab = 0;
    int activeSpriteTab = 0;
    bool restoreToolTab = true;
    bool restoreEventPaletteTab = true;
    bool restoreSpriteTab = true;
};
