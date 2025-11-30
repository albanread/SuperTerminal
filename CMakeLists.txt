cmake_minimum_required(VERSION 3.20)
project(SuperTerminal VERSION 1.0.0 LANGUAGES C CXX OBJCXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform check - macOS only
if(NOT APPLE)
    message(FATAL_ERROR "SuperTerminal requires macOS")
endif()

# Check for Apple Silicon (optional warning)
execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE ARCH_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ARCH_OUTPUT STREQUAL "arm64")
    message(WARNING "SuperTerminal is optimized for Apple Silicon (arm64), but will build for ${ARCH_OUTPUT}")
endif()

# Build configuration
set(CMAKE_BUILD_TYPE_INIT "Release")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)

# Find zstd
find_path(ZSTD_INCLUDE_DIR zstd.h
    HINTS /opt/homebrew/include /usr/local/include
    PATHS /usr/include
)
find_library(ZSTD_LIBRARY
    NAMES zstd
    HINTS /opt/homebrew/lib /usr/local/lib
    PATHS /usr/lib
)

if(NOT ZSTD_INCLUDE_DIR OR NOT ZSTD_LIBRARY)
    message(FATAL_ERROR "zstd not found. Install with: brew install zstd")
endif()

# Find LuaJIT
find_path(LUAJIT_INCLUDE_DIR luajit.h
    HINTS /usr/local/include/luajit-2.1 /opt/homebrew/include/luajit-2.1
    PATHS /usr/include/luajit-2.1
)
find_library(LUAJIT_LIBRARY
    NAMES luajit-5.1 luajit
    HINTS /usr/local/lib /opt/homebrew/lib
    PATHS /usr/lib
)

if(NOT LUAJIT_INCLUDE_DIR OR NOT LUAJIT_LIBRARY)
    message(FATAL_ERROR "LuaJIT not found. Install with: brew install luajit")
endif()

# Find Skia (use our built version from libskia distribution)
set(SKIA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libskia")
set(SKIA_BUILD_DIR "${SKIA_ROOT_DIR}/out/Release")

find_path(SKIA_INCLUDE_DIR include/core/SkCanvas.h
    HINTS ${SKIA_ROOT_DIR}
    NO_DEFAULT_PATH
)
find_library(SKIA_LIBRARY
    NAMES skia
    HINTS ${SKIA_BUILD_DIR}
    NO_DEFAULT_PATH
)

if(NOT SKIA_INCLUDE_DIR OR NOT SKIA_LIBRARY)
    message(WARNING "Skia not found. Make sure to build Skia first with: cd skia && ninja -C out/Release")
    set(USE_SKIA OFF)
else()
    set(USE_SKIA ON)
    set(SKIA_GENERATED_INCLUDE_DIR "${SKIA_BUILD_DIR}/gen")
    message(STATUS "Found built Skia: ${SKIA_LIBRARY}")
    message(STATUS "Skia includes: ${SKIA_INCLUDE_DIR}")
    message(STATUS "Skia generated headers: ${SKIA_GENERATED_INCLUDE_DIR}")
endif()

# Build embedded SQLite3
add_library(sqlite3 STATIC external/sqlite/sqlite3.c)
set_target_properties(sqlite3 PROPERTIES LINKER_LANGUAGE C)
target_include_directories(sqlite3 PUBLIC external/sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_MAX_EXPR_DEPTH=0
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
)
# Suppress warnings from SQLite
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# Source files for the framework
set(FRAMEWORK_SOURCES
    src/SuperTerminalAPI.cpp
    src/SuperTerminalWindow.mm
    src/SuperTerminalMenus.mm
    src/SuperTerminalReset.cpp
    src/MetalRenderer.mm
    src/TextLayer.mm
    src/TextGridManager.mm
    src/CoreTextRenderer.mm
    src/TileLayer.mm
    src/GraphicsLayer.mm
    src/SimpleGraphicsLayer.mm
    src/MinimalGraphicsLayer.mm
    src/SkiaGraphicsLayer.mm
    src/SpriteLayer.mm
    src/SpriteEffectSystem.mm
    src/ParticleSystem.mm
    src/ParticleSystemLua.mm
    src/BulletSystem.mm
    src/BulletSystemLua.mm
    src/CommandQueue.cpp
    src/ConsoleLogger.mm
    src/GlobalShutdown.cpp
    src/SimpleScriptLoader.cpp
    src/DirectScriptLoader.cpp
    src/LuaRuntime.cpp  # Keep for API registration functions
    src/LuaRuntimeGCD.mm
    src/LuaBlockingOpsGCD.cpp
    # src/LuaRuntimeCompat.cpp  # Not needed - using real LuaRuntime.cpp
    src/TextEditor.cpp
    src/GapBuffer.cpp
    src/ReplConsole.cpp
    src/LuaFormatter.cpp

    # New SubsystemManager files
    src/SubsystemManager.cpp
    src/SuperTerminalMain.cpp
    src/LuaSubsystemIntegration.cpp
    src/SubsystemManagerCompat.cpp

    src/OverlayGraphicsLayer.mm
    src/audio/AudioSystem.mm
    src/audio/CoreAudioEngine.mm
    src/audio/SynthEngine.mm
    src/audio/MidiEngine.mm
    src/audio/MusicPlayer.mm
    # src/audio/ABCPlayerClient.cpp  # Commented out - using XPC client instead
    src/audio/AudioLuaBindings.cpp
    src/audio/MidiLuaBindings.mm
    src/audio/MidiEventCapture.mm
    # New v2 audio system (basic files only)
    src/audio/v2/AudioBuffer.cpp
    src/audio/v2/AudioNode.cpp
    src/audio/v2/SynthNode.cpp
    src/audio/v2/SimpleTest.cpp

    # Audio daemon system
    src/audio/daemon/AudioDaemonProtocol.cpp
    src/audio/daemon/MinimalAudioClient.cpp

    # Asset database system
    src/assets/AssetMetadata.cpp
    src/assets/AssetDatabase.cpp
    src/assets/AssetsManager.cpp
    src/assets/AssetDialogs.mm
    src/assets/AssetsLuaBindings.cpp
)

# Metal shader files
set(METAL_SHADERS
    src/shaders/Background.metal
    src/shaders/Tile.metal
    src/shaders/Graphics.metal
    src/shaders/Text.metal
    src/shaders/Sprite.metal
    src/shaders/Particle.metal
    src/shaders/Common.metal
)

# Create SuperTerminal Framework
add_library(SuperTerminal SHARED ${FRAMEWORK_SOURCES})

# Set framework properties
set_target_properties(SuperTerminal PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER com.superterminal.SuperTerminal
    MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/SuperTerminal.framework/Info.plist
    PUBLIC_HEADER "SuperTerminal.framework/Headers/SuperTerminal.h"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Framework include directories
target_include_directories(SuperTerminal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${LUAJIT_INCLUDE_DIR}
    ${ZSTD_INCLUDE_DIR}
)

if(USE_SKIA)
    target_include_directories(SuperTerminal PRIVATE
        ${SKIA_INCLUDE_DIR}
        ${SKIA_GENERATED_INCLUDE_DIR}
    )
    target_compile_definitions(SuperTerminal PRIVATE USE_SKIA=1)
endif()

# Link frameworks and libraries
target_link_libraries(SuperTerminal PRIVATE
    ${LUAJIT_LIBRARY}
    sqlite3
    ${ZSTD_LIBRARY}
    "-framework Cocoa"
    "-framework Metal"
    "-framework MetalKit"
    "-framework CoreGraphics"
    "-framework Foundation"
    "-framework QuartzCore"
    "-framework CoreText"
    "-framework ImageIO"
    "-framework AVFoundation"
    "-framework AudioToolbox"
    "-framework CoreAudio"
    "-framework UniformTypeIdentifiers"
)

if(USE_SKIA)
    target_link_libraries(SuperTerminal PRIVATE
        ${SKIA_LIBRARY}
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework ImageIO"
        "-L/opt/homebrew/opt/webp/lib"
        "-L/opt/homebrew/opt/jpeg-turbo/lib"
        "-L/opt/homebrew/opt/libpng/lib"
        "-L/opt/homebrew/opt/freetype/lib"
        "-L/opt/homebrew/opt/harfbuzz/lib"
        "-L/opt/homebrew/opt/icu4c/lib"
        webp
        webpdemux
        jpeg
        png16
        z
        harfbuzz
        freetype
        icuuc
        icudata
    )
endif()

# Compile Metal shaders
foreach(SHADER ${METAL_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(COMPILED_SHADER "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.metallib")
    add_custom_command(
        OUTPUT ${COMPILED_SHADER}
        COMMAND xcrun -sdk macosx metal -c ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.air
        COMMAND xcrun -sdk macosx metallib ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.air -o ${COMPILED_SHADER}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling Metal shader ${SHADER_NAME}"
    )
    target_sources(SuperTerminal PRIVATE ${COMPILED_SHADER})
endforeach()

# Install framework
install(TARGETS SuperTerminal
    FRAMEWORK DESTINATION /Library/Frameworks
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Create test executable
add_executable(test_superterminal tests/cpp/test_main_luarunner.cpp)
target_link_libraries(test_superterminal PRIVATE SuperTerminal)
target_include_directories(test_superterminal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create main Lua runner
add_executable(luarunner luarunner/main.cpp)
target_link_libraries(luarunner PRIVATE SuperTerminal)
target_include_directories(luarunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create load menu fix test
add_executable(test_load_menu_fix tests/cpp/test_load_menu_fix.cpp)
target_link_libraries(test_load_menu_fix PRIVATE SuperTerminal)
target_include_directories(test_load_menu_fix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create load functionality test
add_executable(test_load_functionality tests/cpp/test_load_functionality.cpp)
target_link_libraries(test_load_functionality PRIVATE SuperTerminal)
target_include_directories(test_load_functionality PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create bulletproof Lua execution test
add_executable(test_bulletproof_direct tests/cpp/test_bulletproof_direct.cpp)
target_link_libraries(test_bulletproof_direct PRIVATE SuperTerminal)
target_include_directories(test_bulletproof_direct PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create interactive Lua runner with menu system as macOS app bundle
add_executable(interactive_luarunner MACOSX_BUNDLE cppdemos/interactive_luarunner.cpp)
target_link_libraries(interactive_luarunner PRIVATE SuperTerminal)
target_include_directories(interactive_luarunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

# Set app bundle properties
set_target_properties(interactive_luarunner PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cppdemos/SuperTerminal-Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "SuperTerminal"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.superterminal.SuperTerminal"
    OUTPUT_NAME "SuperTerminal"
)

# Copy SuperTerminal framework into app bundle
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:SuperTerminal>"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Frameworks/SuperTerminal.framework"
    COMMENT "Embedding SuperTerminal.framework into app bundle"
)

# Copy XPC service into app bundle
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/XPCServices"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:ABCPlayerService>"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/XPCServices/com.superterminal.ABCPlayer.xpc"
    COMMENT "Embedding XPC service into app bundle"
)

# Copy examples into app bundle Resources
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/examples"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/examples"
    COMMENT "Copying examples into app bundle"
)

# Copy assets into app bundle Resources
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/assets"
    COMMENT "Copying assets into app bundle"
)

# Copy font files into app bundle Resources/assets where they're expected
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/font_atlas.png"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/assets/font_atlas.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/font_atlas_8x8.png"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/assets/font_atlas_8x8.png"
    COMMENT "Copying font files into app bundle assets"
)

# Copy PetMe fonts to Resources root for easy NSBundle discovery
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/petme/PetMe64.ttf"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/PetMe64.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/petme/PetMe.ttf"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/PetMe.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/petme/PetMe128.ttf"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/PetMe128.ttf"
    COMMENT "Copying PetMe fonts to Resources root for NSBundle"
)

# Copy Particle.metallib shader into app bundle Resources
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/Particle.metallib"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/Particle.metallib"
    COMMENT "Copying Particle.metallib shader into app bundle"
)

# Copy Sprite.metallib shader into app bundle Resources
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/Sprite.metallib"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/Sprite.metallib"
    COMMENT "Copying Sprite.metallib shader into app bundle"
)

# Update executable's rpath to find embedded framework
set_target_properties(interactive_luarunner PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks"
)

# Make interactive_luarunner depend on both SuperTerminal and ABCPlayerService
add_dependencies(interactive_luarunner SuperTerminal ABCPlayerService)

# Copy test_abc_xpc into app bundle for testing XPC service
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:test_abc_xpc>"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/MacOS/test_abc_xpc"
    COMMENT "Copying test_abc_xpc into app bundle for testing"
)
add_dependencies(interactive_luarunner test_abc_xpc)

# Copy lua-format binary into app bundle for Lua code formatting
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/external/LuaFormatter/lua-format"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/MacOS/lua-format"
    COMMENT "Copying lua-format into app bundle"
)

# Copy .lua-format config file into app bundle
add_custom_command(TARGET interactive_luarunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/.lua-format"
        "$<TARGET_BUNDLE_DIR:interactive_luarunner>/Contents/Resources/.lua-format"
    COMMENT "Copying .lua-format config into app bundle"
)

# Create multiple loads test
add_executable(test_multiple_loads tests/cpp/test_multiple_loads.cpp)
target_link_libraries(test_multiple_loads PRIVATE SuperTerminal)
target_include_directories(test_multiple_loads PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create ABC XPC test
add_executable(test_abc_xpc tests/cpp/test_abc_xpc.cpp)
target_link_libraries(test_abc_xpc PRIVATE SuperTerminal)
target_include_directories(test_abc_xpc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create SubsystemManager demo
add_executable(subsystem_manager_demo examples/subsystem_manager_demo.cpp)
target_link_libraries(subsystem_manager_demo PRIVATE SuperTerminal)
target_include_directories(subsystem_manager_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple SubsystemManager test
add_executable(subsystem_manager_simple_test examples/subsystem_manager_simple_test.cpp)
target_link_libraries(subsystem_manager_simple_test PRIVATE SuperTerminal)
target_include_directories(subsystem_manager_simple_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create asset database test
add_executable(test_asset_database tests/cpp/test_asset_database.cpp)
target_link_libraries(test_asset_database PRIVATE SuperTerminal sqlite3)
target_include_directories(test_asset_database PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create assets manager test
add_executable(test_assets_manager tests/cpp/test_assets_manager.cpp)
target_link_libraries(test_assets_manager PRIVATE SuperTerminal sqlite3)
target_include_directories(test_assets_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create assetdb command-line tool
add_executable(assetdb tools/assetdb/main.cpp)
target_link_libraries(assetdb PRIVATE SuperTerminal sqlite3)
target_include_directories(assetdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create SubsystemManager unit test
add_executable(subsystem_manager_unit_test examples/subsystem_manager_unit_test.cpp)
target_link_libraries(subsystem_manager_unit_test PRIVATE SuperTerminal)
target_include_directories(subsystem_manager_unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple Lua runner
add_executable(simple_luarunner cppdemos/simple_luarunner.cpp)
target_link_libraries(simple_luarunner PRIVATE SuperTerminal)
target_include_directories(simple_luarunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create C++ sprite demo executable
add_executable(sprites_demo examples/sprites_demo.cpp)
target_link_libraries(sprites_demo PRIVATE SuperTerminal)
target_include_directories(sprites_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple sprite animation demo
add_executable(simple_sprite_animation cppdemos/simple_sprite_animation.cpp)
target_link_libraries(simple_sprite_animation PRIVATE SuperTerminal)
target_include_directories(simple_sprite_animation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create minimal sprite test
add_executable(sprite_test_simple tests/cpp/sprite_test_simple.cpp)
target_link_libraries(sprite_test_simple PRIVATE SuperTerminal)
target_include_directories(sprite_test_simple PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create collision detection test
add_executable(collision_detection_test tests/cpp/collision_detection_test.cpp)
target_link_libraries(collision_detection_test PRIVATE SuperTerminal)
target_include_directories(collision_detection_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create non-blocking sprite animation
add_executable(sprite_animation_fixed cppdemos/sprite_animation_fixed.cpp)
target_link_libraries(sprite_animation_fixed PRIVATE SuperTerminal)
target_include_directories(sprite_animation_fixed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create sprite wrapping demo
add_executable(sprite_demo_wrap cppdemos/sprite_demo_wrap.cpp)
target_link_libraries(sprite_demo_wrap PRIVATE SuperTerminal)
target_include_directories(sprite_demo_wrap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create tile layer test
add_executable(tile_test tests/cpp/tile_test.cpp)
target_link_libraries(tile_test PRIVATE SuperTerminal)
target_include_directories(tile_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create real tile assets demo
add_executable(tile_demo_real cppdemos/tile_demo_real.cpp)
target_link_libraries(tile_demo_real PRIVATE SuperTerminal)
target_include_directories(tile_demo_real PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple tile test
add_executable(tile_simple_test tests/cpp/tile_simple_test.cpp)
target_link_libraries(tile_simple_test PRIVATE SuperTerminal)
target_include_directories(tile_simple_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create endless scroll demo
add_executable(tile_endless_scroll cppdemos/tile_endless_scroll.cpp)
target_link_libraries(tile_endless_scroll PRIVATE SuperTerminal)
target_include_directories(tile_endless_scroll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create fast scroll demo
add_executable(tile_fast_scroll cppdemos/tile_fast_scroll.cpp)
target_link_libraries(tile_fast_scroll PRIVATE SuperTerminal)
target_include_directories(tile_fast_scroll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})



# Create parallax demo
add_executable(tile_parallax_demo cppdemos/tile_parallax_demo.cpp)
target_link_libraries(tile_parallax_demo PRIVATE SuperTerminal)
target_include_directories(tile_parallax_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create advanced parallax demo
add_executable(advanced_parallax_demo cppdemos/advanced_parallax_demo.cpp)
target_link_libraries(advanced_parallax_demo PRIVATE SuperTerminal)
target_include_directories(advanced_parallax_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create parallax techniques demo
add_executable(parallax_techniques_demo cppdemos/parallax_techniques_demo.cpp)
target_link_libraries(parallax_techniques_demo PRIVATE SuperTerminal)
target_include_directories(parallax_techniques_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create parallax showcase demo
add_executable(parallax_showcase cppdemos/parallax_showcase.cpp)
target_link_libraries(parallax_showcase PRIVATE SuperTerminal)
target_include_directories(parallax_showcase PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create interactive parallax demo
add_executable(interactive_parallax_demo cppdemos/interactive_parallax_demo.cpp)
target_link_libraries(interactive_parallax_demo PRIVATE SuperTerminal)
target_include_directories(interactive_parallax_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create input test demo
add_executable(input_test_demo cppdemos/input_test_demo.cpp)
target_link_libraries(input_test_demo PRIVATE SuperTerminal)
target_include_directories(input_test_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple sprite movement test
add_executable(simple_sprite_move_test tests/cpp/simple_sprite_move_test.cpp)
target_link_libraries(simple_sprite_move_test PRIVATE SuperTerminal)
target_include_directories(simple_sprite_move_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create sprite cascade demo
add_executable(sprite_cascade_demo cppdemos/sprite_cascade_demo.cpp)
target_link_libraries(sprite_cascade_demo PRIVATE SuperTerminal)
target_include_directories(sprite_cascade_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create sprite position bug test
add_executable(sprite_position_test tests/cpp/sprite_position_test.cpp)
target_link_libraries(sprite_position_test PRIVATE SuperTerminal)
target_include_directories(sprite_position_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create ABC notation command queue fix test
add_executable(test_abc_fix tests/cpp/test_abc_fix.cpp)
target_link_libraries(test_abc_fix PRIVATE SuperTerminal)
target_include_directories(test_abc_fix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create MIDI command flow debug test
add_executable(debug_midi_commands tests/cpp/debug_midi_commands.cpp)
target_link_libraries(debug_midi_commands PRIVATE SuperTerminal)
target_include_directories(debug_midi_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple ABC notation test
add_executable(simple_abc_test tests/cpp/simple_abc_test.cpp)
target_link_libraries(simple_abc_test PRIVATE SuperTerminal)
target_include_directories(simple_abc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create all features showcase demo
add_executable(all_features_showcase cppdemos/all_features_showcase.cpp)
target_link_libraries(all_features_showcase PRIVATE SuperTerminal)
target_include_directories(all_features_showcase PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create debug all systems demo
add_executable(debug_all_systems cppdemos/debug_all_systems.cpp)
target_link_libraries(debug_all_systems PRIVATE SuperTerminal)
target_include_directories(debug_all_systems PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create graphics only test
add_executable(graphics_only_test tests/cpp/graphics_only_test.cpp)
target_link_libraries(graphics_only_test PRIVATE SuperTerminal)
target_include_directories(graphics_only_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create layer toggle test
add_executable(layer_toggle_test tests/cpp/layer_toggle_test.cpp)
target_link_libraries(layer_toggle_test PRIVATE SuperTerminal)
target_include_directories(layer_toggle_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create lua test
add_executable(lua_test tests/cpp/lua_test.cpp)
target_link_libraries(lua_test PRIVATE SuperTerminal)
target_include_directories(lua_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create editor test app
add_executable(editor_test_app tests/cpp/editor_test_app.cpp)
target_link_libraries(editor_test_app PRIVATE SuperTerminal)
target_include_directories(editor_test_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create direct reset test
add_executable(test_direct_reset tests/cpp/test_direct_reset.cpp)
target_link_libraries(test_direct_reset PRIVATE SuperTerminal)
target_include_directories(test_direct_reset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create F1 editor debug test
add_executable(debug_f1_editor cppdemos/debug_f1_editor.cpp)
target_link_libraries(debug_f1_editor PRIVATE SuperTerminal)
target_include_directories(debug_f1_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create editor visibility test
add_executable(test_editor_visible tests/cpp/test_editor_visible.cpp)
target_link_libraries(test_editor_visible PRIVATE SuperTerminal)
target_include_directories(test_editor_visible PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple ESC test
add_executable(simple_esc_test tests/cpp/simple_esc_test.cpp)
target_link_libraries(simple_esc_test PRIVATE SuperTerminal)
target_include_directories(simple_esc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create minimal editor test
add_executable(minimal_editor_test tests/cpp/minimal_editor_test.cpp)
target_link_libraries(minimal_editor_test PRIVATE SuperTerminal)
target_include_directories(minimal_editor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create debug key codes test
add_executable(debug_key_codes cppdemos/debug_key_codes.cpp)
target_link_libraries(debug_key_codes PRIVATE SuperTerminal)
target_include_directories(debug_key_codes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple key test
add_executable(simple_key_test tests/cpp/simple_key_test.cpp)
target_link_libraries(simple_key_test PRIVATE SuperTerminal)
target_include_directories(simple_key_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create direct editor test
add_executable(test_editor_direct tests/cpp/test_editor_direct.cpp)
target_link_libraries(test_editor_direct PRIVATE SuperTerminal)
target_include_directories(test_editor_direct PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create basic terminal test
add_executable(basic_terminal_test tests/cpp/basic_terminal_test.cpp)
target_link_libraries(basic_terminal_test PRIVATE SuperTerminal)
target_include_directories(basic_terminal_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create sprite explosion demo
add_executable(sprite_explosion_demo cppdemos/sprite_explosion_demo.cpp)
target_link_libraries(sprite_explosion_demo PRIVATE SuperTerminal)
target_include_directories(sprite_explosion_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple particle test
add_executable(simple_particle_test cppdemos/simple_particle_test.cpp)
target_link_libraries(simple_particle_test PRIVATE SuperTerminal)
target_include_directories(simple_particle_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple audio test
add_executable(simple_audio_test cppdemos/simple_audio_test.cpp)
target_link_libraries(simple_audio_test PRIVATE SuperTerminal)
target_include_directories(simple_audio_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple music test
add_executable(simple_music_test cppdemos/deprecated/simple_music_test.cpp)
target_link_libraries(simple_music_test PRIVATE SuperTerminal)
target_include_directories(simple_music_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create debug audio test
add_executable(debug_audio_test cppdemos/debug_audio_test.cpp)
target_link_libraries(debug_audio_test PRIVATE SuperTerminal)
target_include_directories(debug_audio_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create auto music test
add_executable(auto_music_test cppdemos/deprecated/auto_music_test.cpp)
target_link_libraries(auto_music_test PRIVATE SuperTerminal)
target_include_directories(auto_music_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple MIDI test
add_executable(simple_midi_test cppdemos/simple_midi_test.cpp)
target_link_libraries(simple_midi_test PRIVATE SuperTerminal)
target_include_directories(simple_midi_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create MIDI plus sound effects test
add_executable(midi_plus_sfx_test cppdemos/deprecated/midi_plus_sfx_test.cpp)
target_link_libraries(midi_plus_sfx_test PRIVATE SuperTerminal)
target_include_directories(midi_plus_sfx_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create MIDI plus sound effects test (fixed key input)
add_executable(midi_plus_sfx_test_fixed cppdemos/deprecated/midi_plus_sfx_test_fixed.cpp)
target_link_libraries(midi_plus_sfx_test_fixed PRIVATE SuperTerminal)
target_include_directories(midi_plus_sfx_test_fixed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create MIDI plus sound effects test (simple fix)
add_executable(midi_plus_sfx_simple_fix cppdemos/deprecated/midi_plus_sfx_simple_fix.cpp)
target_link_libraries(midi_plus_sfx_simple_fix PRIVATE SuperTerminal)
target_include_directories(midi_plus_sfx_simple_fix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create MIDI plus sound effects test (working version with correct key codes)
add_executable(midi_plus_sfx_working cppdemos/deprecated/midi_plus_sfx_working.cpp)
target_link_libraries(midi_plus_sfx_working PRIVATE SuperTerminal)
target_include_directories(midi_plus_sfx_working PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create MIDI plus sound effects test (final version with proper key state detection)
add_executable(midi_plus_sfx_final cppdemos/midi_plus_sfx_final.cpp)
target_link_libraries(midi_plus_sfx_final PRIVATE SuperTerminal)
target_include_directories(midi_plus_sfx_final PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create music API test
add_executable(music_api_test cppdemos/music_api_test.cpp)
target_link_libraries(music_api_test PRIVATE SuperTerminal)
target_include_directories(music_api_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create music slots demo
add_executable(music_slots_demo cppdemos/music_slots_demo.cpp)
target_link_libraries(music_slots_demo PRIVATE SuperTerminal)
target_include_directories(music_slots_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create enhanced ABC parser test
add_executable(test_abc_enhanced tests/cpp/test_abc_enhanced.cpp)
target_link_libraries(test_abc_enhanced PRIVATE SuperTerminal)
target_include_directories(test_abc_enhanced PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_multi_instrument tests/cpp/test_multi_instrument.cpp)
target_link_libraries(test_multi_instrument PRIVATE SuperTerminal)
target_include_directories(test_multi_instrument PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create automatic music test
add_executable(music_auto_test cppdemos/music_auto_test.cpp)
target_link_libraries(music_auto_test PRIVATE SuperTerminal)
target_include_directories(music_auto_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create debug music parsing demo
add_executable(debug_music_parsing cppdemos/debug_music_parsing.cpp)
target_link_libraries(debug_music_parsing PRIVATE SuperTerminal)
target_include_directories(debug_music_parsing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create standalone ABC parser test
add_executable(standalone_abc_parser_test cppdemos/standalone_abc_parser_test.cpp)
target_link_libraries(standalone_abc_parser_test PRIVATE SuperTerminal)
target_include_directories(standalone_abc_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create comprehensive ABC test
add_executable(comprehensive_abc_test cppdemos/comprehensive_abc_test.cpp)
target_link_libraries(comprehensive_abc_test PRIVATE SuperTerminal)
target_include_directories(comprehensive_abc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create Lua text editor test - DISABLED due to obsolete editor function references
# add_executable(test_lua_editor test_lua_editor.cpp)
# target_link_libraries(test_lua_editor PRIVATE SuperTerminal)
# target_include_directories(test_lua_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create minimal Lua editor test - DISABLED due to obsolete editor function references
# add_executable(minimal_lua_test minimal_lua_test.cpp)
# target_link_libraries(minimal_lua_test PRIVATE SuperTerminal)
# target_include_directories(minimal_lua_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create audio shutdown diagnostic test
add_executable(test_audio_shutdown_diagnostic tests/cpp/test_audio_shutdown_diagnostic.cpp)
target_link_libraries(test_audio_shutdown_diagnostic PRIVATE SuperTerminal)
target_include_directories(test_audio_shutdown_diagnostic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create emergency flag test
add_executable(test_exec_lua_emergency_flag tests/cpp/test_exec_lua_emergency_flag.cpp)
target_link_libraries(test_exec_lua_emergency_flag PRIVATE SuperTerminal)
target_include_directories(test_exec_lua_emergency_flag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create script loading debugger
add_executable(debug_script_loading debug_script_loading.cpp)
target_link_libraries(debug_script_loading PRIVATE SuperTerminal)
target_include_directories(debug_script_loading PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create multiple scripts test
add_executable(test_multiple_scripts tests/cpp/test_multiple_scripts.cpp)
target_link_libraries(test_multiple_scripts PRIVATE SuperTerminal)
target_include_directories(test_multiple_scripts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create direct loader test
add_executable(test_direct_loader tests/cpp/test_direct_loader.cpp)
target_link_libraries(test_direct_loader PRIVATE SuperTerminal)
target_include_directories(test_direct_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create raw Lua test (minimal, no SuperTerminal dependencies)
add_executable(test_raw_lua tests/cpp/test_raw_lua.cpp)
target_link_libraries(test_raw_lua PRIVATE ${LUAJIT_LIBRARY})
target_include_directories(test_raw_lua PRIVATE ${LUAJIT_INCLUDE_DIR})

# Create main thread music test
add_executable(main_thread_music_test cppdemos/deprecated/main_thread_music_test.cpp)
target_link_libraries(main_thread_music_test PRIVATE SuperTerminal)
target_include_directories(main_thread_music_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create working music test (direct MIDI)
add_executable(working_music_test cppdemos/deprecated/working_music_test.cpp)
target_link_libraries(working_music_test PRIVATE SuperTerminal)
target_include_directories(working_music_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create explosion modes demo
add_executable(explosion_modes_demo cppdemos/explosion_modes_demo.cpp)
target_link_libraries(explosion_modes_demo PRIVATE SuperTerminal)
target_include_directories(explosion_modes_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create shutdown test demo
add_executable(shutdown_test_demo cppdemos/shutdown_test_demo.cpp)
target_link_libraries(shutdown_test_demo PRIVATE SuperTerminal)
target_include_directories(shutdown_test_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create shutdown order test
add_executable(test_shutdown_order tests/cpp/test_shutdown_order.cpp)
target_link_libraries(test_shutdown_order PRIVATE SuperTerminal)
target_include_directories(test_shutdown_order PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create ABC multi-voice demo
add_executable(abc_multi_voice_demo cppdemos/abc_multi_voice_demo.cpp)
target_link_libraries(abc_multi_voice_demo PRIVATE SuperTerminal)
target_include_directories(abc_multi_voice_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create Hark Herald parser test
add_executable(hark_herald_parser_test cppdemos/hark_herald_parser_test.cpp)
target_link_libraries(hark_herald_parser_test PRIVATE SuperTerminal)
target_include_directories(hark_herald_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create voice channel debug test
add_executable(voice_channel_debug cppdemos/voice_channel_debug.cpp)
target_link_libraries(voice_channel_debug PRIVATE SuperTerminal)
target_include_directories(voice_channel_debug PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create console voice test
add_executable(console_voice_test cppdemos/console_voice_test.cpp)
target_link_libraries(console_voice_test PRIVATE SuperTerminal)
target_include_directories(console_voice_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple voice test
add_executable(simple_voice_test cppdemos/simple_voice_test.cpp)
target_link_libraries(simple_voice_test PRIVATE SuperTerminal)
target_include_directories(simple_voice_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create Lua formatter test
add_executable(lua_formatter_test tests/cpp/lua_formatter_test.cpp)
target_link_libraries(lua_formatter_test PRIVATE SuperTerminal)
target_include_directories(lua_formatter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})



# Copy fonts to build directory for development
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/PetMe.ttf
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/PetMe64.ttf
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/PetMe128.ttf
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/PetMe2X.ttf
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/)

# Copy tile assets to build directory for development
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/tiles/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/tiles/)

# Copy sprite assets to build directory for development
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/sprites/)

# Custom target for development
add_custom_target(dev
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target SuperTerminal
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target luarunner
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building SuperTerminal for development"
)

# Custom target to run test
# Create simple audio v2 test
add_executable(test_audio_v2 tests/cpp/test_audio_v2.cpp)
target_link_libraries(test_audio_v2 PRIVATE SuperTerminal)
target_include_directories(test_audio_v2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create real-time MIDI visualizer
add_executable(realtime_midi_visualizer cppdemos/realtime_midi_visualizer.cpp)
target_link_libraries(realtime_midi_visualizer PRIVATE SuperTerminal)
target_include_directories(realtime_midi_visualizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create simple MIDI visualizer (deadlock-free)
add_executable(simple_midi_visualizer cppdemos/simple_midi_visualizer.cpp)
target_link_libraries(simple_midi_visualizer PRIVATE SuperTerminal)
target_include_directories(simple_midi_visualizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create audio daemon executable
add_executable(superterminal_audiod src/audio/daemon/superterminal_audiod.cpp)
target_link_libraries(superterminal_audiod PRIVATE SuperTerminal)
target_include_directories(superterminal_audiod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Create audio daemon test client
add_executable(test_audio_daemon_client tests/cpp/test_audio_daemon_client.cpp)
target_link_libraries(test_audio_daemon_client PRIVATE SuperTerminal)
target_include_directories(test_audio_daemon_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(run-test
    COMMAND ./test_superterminal
    DEPENDS test_superterminal
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running SuperTerminal test"
)

# Custom target to run audio v2 test
add_custom_target(run-audio-v2-test
    COMMAND ./test_audio_v2
    DEPENDS test_audio_v2
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running SuperTerminal Audio v2.0 Test"
)

# Custom target to run sprite demo
add_custom_target(run-sprites
    COMMAND ./sprites_demo
    DEPENDS sprites_demo
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running SuperTerminal C++ Sprite Demo"
)

# Optional: Standalone ABC Music Player
option(BUILD_STANDALONE_ABC_PLAYER "Build standalone ABC music player" OFF)
if(BUILD_STANDALONE_ABC_PLAYER)
    message(STATUS "Building Standalone ABC Music Player")
    add_subdirectory(standalone_abc_player)
endif()

# Package configuration
set(CPACK_PACKAGE_NAME "SuperTerminal")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Retro computing framework for macOS")
set(CPACK_PACKAGE_VENDOR "SuperTerminal Project")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "SuperTerminal-${PROJECT_VERSION}-macOS")

include(CPack)

# Print configuration summary
message(STATUS "SuperTerminal Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Architecture: ${ARCH_OUTPUT}")
message(STATUS "  LuaJIT: ${LUAJIT_LIBRARY}")
message(STATUS "  Skia Graphics: ${USE_SKIA}")
if(USE_SKIA)
    message(STATUS "  Skia Library: ${SKIA_LIBRARY}")
endif()
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")

# Copy ABC server executable to framework directory for easy access
add_custom_command(TARGET SuperTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/abc_socket_player_queue"
        "${CMAKE_CURRENT_BINARY_DIR}/"
    COMMENT "Copying ABC server executable to build directory"
)

# No longer needed - ABC player is now integrated via XPC service

# ============================================================================
# ABC Player Library
# ============================================================================

# ABC Player static library
add_library(ABCPlayer STATIC
    src/audio/abc/ABCParser.cpp
    src/audio/abc/ABCHeaderParser.cpp
    src/audio/abc/ABCMusicParser.cpp
    src/audio/abc/ABCPlayer.cpp
    src/audio/abc/ABCVoiceManager.cpp
    src/audio/abc/MIDIGenerator.cpp
    src/audio/abc/expand_abc_repeats.cpp
)

target_include_directories(ABCPlayer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/abc
)

target_link_libraries(ABCPlayer PRIVATE
    "-framework AudioToolbox"
    "-framework CoreMIDI"
    "-framework CoreAudio"
    "-framework CoreFoundation"
)

set_target_properties(ABCPlayer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# ============================================================================
# XPC Service: ABCPlayerService
# ============================================================================

# XPC Service executable (must use MACOSX_BUNDLE to create .xpc bundle)
add_executable(ABCPlayerService MACOSX_BUNDLE
    src/audio/xpc/ABCPlayerService.mm
    src/audio/xpc/ABCPlayerServiceProtocol.m
)

# Set XPC service properties
set_target_properties(ABCPlayerService PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "xpc"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/xpc/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "ABCPlayer"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.superterminal.ABCPlayer"
    OUTPUT_NAME "com.superterminal.ABCPlayer"
    MACOSX_BUNDLE_EXECUTABLE_NAME "com.superterminal.ABCPlayer"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Link ABC Player library and frameworks for XPC service
target_link_libraries(ABCPlayerService PRIVATE
    ABCPlayer
    "-framework Foundation"
    "-framework AudioToolbox"
    "-framework CoreMIDI"
    "-framework CoreAudio"
    "-framework CoreFoundation"
)

# Include directories for XPC service
target_include_directories(ABCPlayerService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/xpc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/abc
)

# Install XPC service into framework's XPCServices directory
install(TARGETS ABCPlayerService
    BUNDLE DESTINATION /Library/Frameworks/SuperTerminal.framework/Versions/A/XPCServices
)

# Add XPC client to SuperTerminal framework
target_sources(SuperTerminal PRIVATE
    src/audio/ABCPlayerXPCClient.mm
    src/audio/xpc/ABCPlayerServiceProtocol.m
)

# Add XPC service include path to framework
target_include_directories(SuperTerminal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/xpc
)

# Install programs (moved to end after all executables are defined)
install(TARGETS test_superterminal sprites_demo simple_sprite_animation sprite_test_simple sprite_animation_fixed sprite_demo_wrap tile_test tile_demo_real tile_simple_test tile_endless_scroll tile_fast_scroll tile_parallax_demo advanced_parallax_demo parallax_techniques_demo parallax_showcase interactive_parallax_demo input_test_demo simple_sprite_move_test sprite_cascade_demo sprite_position_test all_features_showcase debug_all_systems graphics_only_test layer_toggle_test lua_test editor_test_app debug_f1_editor test_editor_visible simple_esc_test minimal_editor_test basic_terminal_test simple_particle_test simple_audio_test simple_music_test debug_audio_test auto_music_test simple_midi_test midi_plus_sfx_test midi_plus_sfx_test_fixed midi_plus_sfx_simple_fix midi_plus_sfx_working midi_plus_sfx_final music_api_test music_slots_demo music_auto_test debug_music_parsing standalone_abc_parser_test comprehensive_abc_test test_abc_enhanced test_multi_instrument abc_multi_voice_demo hark_herald_parser_test voice_channel_debug console_voice_test explosion_modes_demo shutdown_test_demo superterminal_audiod test_audio_daemon_client DESTINATION bin)

# Install examples
install(DIRECTORY examples/ DESTINATION share/superterminal/examples
    FILES_MATCHING PATTERN "*.lua"
)

# Install assets
install(DIRECTORY assets/ DESTINATION share/superterminal/assets
    FILES_MATCHING
    PATTERN "*.png"
    PATTERN "*.ttf"
    PATTERN "*.otf"
)
