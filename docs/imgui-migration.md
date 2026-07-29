# SC4Ed ImGui Migration

This copy now has a separate `SC4EdImGui` executable that uses Dear ImGui with a Win32 + Direct3D 11 backend. The old SC4Ed project is left intact so it can remain the behavioral reference while the new UI is ported panel by panel.

## Why this stack

- Dear ImGui removes the slow dialog/control-heavy UI path while staying native and lightweight.
- Direct3D 11 is available through the Windows SDK, so the editor does not need SDL, GLFW, or a runtime DLL just to open a window.
- Docking lets palette, event, tile, block, sprite, property, checkpoint, and emulator views become rearrangeable panels instead of modal dialogs.

## Build

```powershell
cmake --preset vs2022-win32
cmake --build --preset imgui-debug-win32
```

The executable is generated under `build/imgui-vs2022-win32/Debug/SC4EdImGui.exe`.

## Porting order

1. Keep `SC4Core`, `SNESCore`, `CompressionCore`, and the ROM load/save code as the data model.
2. Split rendering away from `HDC`/`HBITMAP` so level, tile, sprite, and emulator frames can upload into D3D11 textures.
3. Move the main level viewport from `SC4ED.cpp` into the ImGui `Level View` panel.
4. Convert dialog procedures into dockable tools:
   - `EventProc.cpp`
   - `PaletteProc.cpp`
   - `TileProc.cpp`
   - `BlockProc.cpp`
   - `SceneProc.cpp`
   - `MapProc.cpp`
   - `SpriteProc.cpp`
   - `CheckpointProc.cpp`
   - `PropertyProc.cpp`
5. Replace the Win32 toolbar and menu command ids with direct ImGui actions.
6. Keep the old editor around until save/load parity is verified on known ROMs.

## Current status

- New ImGui app shell builds independently from the old Visual Studio project.
- ROM open dialog reads basic ROM metadata and CRC32.
- Dockable editor panels are in place as migration anchors.
- The new app is intentionally split into `main`, `D3D11Host`, `RomInfo`, `EditorState`, and `Ui` files so the rewrite does not recreate the `SC4ED.cpp` monolith.
- The level view now renders loaded level data through a D3D11 texture generated from `SC4Core` data. Collision/event overlays are still next.
