#pragma once

#include <string>

struct EditorState;
struct HWND__;
using HWND = HWND__*;

void DrawInstrumentEditor(EditorState& state, HWND hwnd, std::string& logMessage);
void ResetInstrumentEditor();
