#pragma once

#include "EditorState.h"

#include <utility>

static inline void CommitUndoSnapshot(EditorState& state, RomUndoSnapshot snapshot)
{
    constexpr size_t kMaxUndoSnapshots = 24;
    state.undoStack.push_back(std::move(snapshot));
    if (state.undoStack.size() > kMaxUndoSnapshots) {
        state.undoStack.erase(state.undoStack.begin());
    }
    state.redoStack.clear();
    state.session.BeginEdit();
}

static inline void PushUndo(EditorState& state)
{
    if (!state.session.IsLoaded()) {
        return;
    }
    CommitUndoSnapshot(state, state.session.CreateUndoSnapshot(state.selectedEventIndex));
}

static inline bool PerformUndo(EditorState& state)
{
    if (!state.session.IsLoaded() || state.undoStack.empty()) {
        return false;
    }

    state.redoStack.push_back(state.session.CreateUndoSnapshot(state.selectedEventIndex));
    RomUndoSnapshot snapshot = std::move(state.undoStack.back());
    state.undoStack.pop_back();
    state.session.RestoreUndoSnapshot(snapshot, state.selectedEventIndex);
    state.levelRenderer.Invalidate();
    state.levelPaintUndoActive = false;
    state.spritePaintUndoActive = false;
    return true;
}

static inline bool PerformRedo(EditorState& state)
{
    if (!state.session.IsLoaded() || state.redoStack.empty()) {
        return false;
    }

    constexpr size_t kMaxUndoSnapshots = 24;
    state.undoStack.push_back(state.session.CreateUndoSnapshot(state.selectedEventIndex));
    if (state.undoStack.size() > kMaxUndoSnapshots) {
        state.undoStack.erase(state.undoStack.begin());
    }

    RomUndoSnapshot snapshot = std::move(state.redoStack.back());
    state.redoStack.pop_back();
    state.session.RestoreUndoSnapshot(snapshot, state.selectedEventIndex);
    state.levelRenderer.Invalidate();
    state.levelPaintUndoActive = false;
    state.spritePaintUndoActive = false;
    return true;
}
