#pragma once

#include <string>

struct EditorState;
struct HWND__;
using HWND = HWND__*;

// Draws the CV4 sequence inspector and MIDI import/export tools.
void DrawMusicEditor(EditorState& state, HWND hwnd, std::string& logMessage);
void ResetMusicEditor();

