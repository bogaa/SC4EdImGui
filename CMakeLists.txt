cmake_minimum_required(VERSION 3.24)

project(SC4EdImgui LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui was not found at third_party/imgui.")
endif()


set(APP_SOURCES
    src/app/BpsPatch.cpp
    src/app/D3D11Host.cpp
    src/app/EventNames.cpp
    src/app/HudEditor.cpp
    src/app/ImageClipboard.cpp
    src/app/InstrumentEditor.cpp
    src/app/LevelRenderer.cpp
    src/app/MusicEditor.cpp
    src/app/main.cpp
    src/app/PropertyPanel.cpp
    src/app/RomInfo.cpp
    src/app/RomSession.cpp
    src/app/SpriteEditor.cpp
    src/app/Ui.cpp
    src/app/Win32Stubs.cpp
    src/app/SC4EdImGui.rc
)

set(CORE_SOURCES
    src/core/CompressionCore.cpp
    src/core/Emulator.cpp
    src/core/libretrow.cpp
    src/core/NDSCore.cpp
    src/core/SC4Core.cpp
    src/core/SC4EDCore.cpp
    src/core/SNESCore.cpp
)

set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_dx11.cpp
    third_party/imgui/imgui_impl_win32.cpp
)

add_executable(SC4EdImGui WIN32
    ${APP_SOURCES}
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(SC4EdImGui PRIVATE
    src/app
    src/core
    third_party/imgui
)

target_compile_definitions(SC4EdImGui PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(SC4EdImGui PRIVATE
    /FI"${APP_DIR}/LegacyCompat.h"
)

set_source_files_properties(
    src/app/LevelRenderer.cpp
    src/app/RomSession.cpp
    ${CORE_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS WIN32X
)

target_link_libraries(SC4EdImGui PRIVATE
    d3d11
    dxgi
    dsound
    comdlg32
    winmm
    windowscodecs
    shell32
)

if(BUILD_TESTING)
    add_executable(BpsPatchTests
        tests/BpsPatchTests.cpp
        src/app/BpsPatch.cpp
    )
    target_include_directories(BpsPatchTests PRIVATE src/app)
    add_test(NAME BpsPatchRoundTrip COMMAND BpsPatchTests)

endif()

set(RETRO_DLL "${CMAKE_CURRENT_SOURCE_DIR}/runtime/retro.dll")
if(EXISTS "${RETRO_DLL}")
    add_custom_command(TARGET SC4EdImGui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RETRO_DLL}"
            "$<TARGET_FILE_DIR:SC4EdImGui>/retro.dll"
        COMMENT "Copying retro.dll beside SC4EdImGui.exe"
    )
endif()
