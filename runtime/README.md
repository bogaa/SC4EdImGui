This folder is for local runtime files that are needed to run the editor but should not be committed.

`retro.dll` is the libretro SNES core used by the internal emulator. CMake copies it beside `SC4EdImGui.exe` after a build when the DLL exists here.
