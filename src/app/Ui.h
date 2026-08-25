#pragma once

#include "EditorState.h"

#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>

void ApplySc4Style();
bool ConfirmUnsavedChanges(EditorState& state, HWND hwnd, const char* action);
void DrawEditorUi(EditorState& state, HWND hwnd, ID3D11Device* device, const std::vector<std::wstring>& droppedFiles);
