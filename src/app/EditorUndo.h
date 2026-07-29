#pragma once

#include "EditorState.h"

#include <utility>

static inline void PushUndo(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        return;
    }

    constexpr size_t kMaxUndoSnapshots = 24;
    state.undoStack.push_back(state.session.CreateUndoSnapshot(state.selectedEventIndex));
    if (state.undoStack.size() > kMaxUndoSnapshots) {
        state.undoStack.erase(state.undoStack.begin());
    }
}

static inline bool PerformUndo(EditorState& state)
{
    if (!state.session.IsLoaded() || state.undoStack.empty()) {
        return false;
    }

    RomUndoSnapshot snapshot = std::move(state.undoStack.back());
    state.undoStack.pop_back();
    state.session.RestoreUndoSnapshot(snapshot, state.selectedEventIndex);
    state.levelRenderer.Invalidate();
    state.levelPaintUndoActive = false;
    state.spritePaintUndoActive = false;
    return true;
}
