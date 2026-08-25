# SC4Ed ImGui

SC4Ed ImGui is a modern Dear ImGui/Direct3D 11 editor for Super Castlevania IV ROM hacking. It keeps the proven ROM parsing and editing code from the original Win32/MFC SC4Ed, but moves the active editor UI into a faster docked ImGui application.

The original SC4Ed was developed by [RedGuyyyy](https://github.com/RedGuyyyy?tab=repositories), with more project history on the [RHDN forum thread](https://www.romhacking.net/forum/index.php?topic=21867.msg336111#msg336111). SC4Ed was built on ideas from [MegaEDX](https://github.com/Xeeynamo/MegaEdX/tree/master).

## Features

- Docked level editor with foreground/background painting, collision, events, grid, camera bounds, and tile layer priority.
- Event placement, selection, drag/copy, sorting, editing, and ROM persistence.
- Block, tile behavior, palette, sprite, HUD, and scratch-board image editors.
- Music editor with MIDI import/export, track mapping, instruments, volume, tempo, notes, and loop markers.
- BRR instrument sample replacement from WAV files for all 20 CV4 instruments.
- Internal emulator view, undo/redo, unsaved-change protection, ROM expansion, and BPS patch export.

## Layout

- `src/app/` - the ImGui editor application.
- `src/core/` - the retained SC4Ed ROM/core code used by the app.
- `third_party/imgui/` - the minimal Dear ImGui files and Win32/DX11 backend files used by the editor.
- `runtime/` - local runtime DLLs such as `retro.dll`. DLLs are ignored and should not be committed.

ROM files are intentionally ignored and should not be committed.

## Build

Requirements:

- Visual Studio 2022 with the C++ desktop workload
- CMake 3.24 or newer
- Dear ImGui sources in `third_party/imgui`

Configure and build the ImGui editor:

```powershell
cmake --preset vs2022-win32
cmake --build --preset imgui-debug-win32
```

The executable is written under `build/Debug/SC4EdImGui.exe`.

For a distributable build:

```powershell
cmake --build build --config Release
```

The executable is written under `build/Release/SC4EdImGui.exe`.

## Notes

The ImGui rewrite is the active editor. The old Win32/MFC application, randomizer, and standalone expander projects have been removed from this tree; the needed ROM knowledge remains in the retained core files.
