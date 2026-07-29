# Splitting `SC4ED.cpp`

`SC4ED.cpp` is doing too many jobs at once: app state, UI layout, input handling, drawing, editor commands, modal dialog launch, level editing, event editing, sprite editing, tile editing, emulator refresh, and save/load command handling. The ImGui rewrite should not repeat that structure.

## Target layout

Use small files that match what a person is trying to change:

- `App/MainWindow.*`: window lifetime, app loop, shutdown.
- `App/Commands.*`: open/save/run emulator/menu actions.
- `Model/EditorState.*`: selected level, checkpoint, view toggles, editor mode, camera, drag state.
- `Model/RomSession.*`: owns `SC4Core`, load/save, current ROM path, dirty state.
- `Rendering/LevelRenderer.*`: level, background, collision, checkpoint, and event overlay rendering.
- `Rendering/TextureCache.*`: D3D11 texture upload/cache replacement for `HBITMAP`.
- `Tools/LevelTool.*`: level painting and block picking.
- `Tools/EventTool.*`: event palette, event drag/drop, clone/delete.
- `Tools/SpriteTool.*`: sprite/character editor state and drawing.
- `Tools/TileTool.*`: raw tile editing and tile palette controls.
- `Panels/*.cpp`: ImGui panels only; they call tools/model methods but do not own ROM logic.

## First extraction from the old file

Start with low-risk state and helpers before moving behavior:

1. Move constants like toolbar heights, palette item sizes, and editor enum types into `EditorState.h`.
2. Move pure coordinate helpers into `LevelView.*`:
   - `GetLevelPixelWidth`
   - `GetLevelPixelHeight`
   - `GetLevelBlockFromClientPoint`
   - `GetLevelBlockMappingIndex`
   - `GetLevelBlockAtClientPoint`
   - `ClampCamera`
3. Move ROM mutation helpers into `RomSession.*`:
   - `WriteExpandedRamToRom`
   - `SetLevelBlockAtClientPoint`
4. Move event palette structures and logic into `EventTool.*`.
5. Move character/sprite/tile editor globals into their own tool state structs.

## Rule for each split

Each new file should have one owner struct/class. Avoid moving a pile of globals into another pile of globals. If a function needs `nmmx`, camera position, or draw flags, pass a state object or make it a method on the state owner.

## Bridge period

During the port, the old Win32 build can keep `SC4ED.cpp`. The new ImGui app should import concepts, not copy the monolith. When behavior is ported, prefer:

```cpp
state.eventTool.DrawPanel(session);
state.levelTool.HandleViewportInput(session, viewport);
renderer.DrawLevel(session, state.camera, state.viewOptions);
```

over large free functions that reach through global variables.
