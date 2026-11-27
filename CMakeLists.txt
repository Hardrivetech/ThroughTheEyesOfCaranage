cmake_minimum_required(VERSION 3.24)
project(ThroughTheEyesOfCarnage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- GLFW ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# --- GLM ---
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# --- GLAD2 (generated at configure time; requires Python + jinja2) ---
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.8
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake)

FetchContent_MakeAvailable(glfw glm glad)

glad_add_library(glad_gl_core_46 STATIC REPRODUCIBLE LOADER API gl:core=4.6)

add_executable(Carnage
    src/main.cpp
    src/core/Memory.cpp
    src/core/Log.cpp
    src/core/Renderer.cpp
    src/core/Input.cpp
    src/core/Audio.cpp
    src/game/Camera.cpp
    src/game/Trauma.cpp
    src/game/CameraFeel.cpp
    src/game/World.cpp
    src/game/Movement.cpp
    src/game/Combat.cpp
    src/game/Weapon.cpp
    src/game/Weapons.cpp
    src/game/Effects.cpp
    src/game/Hud.cpp
    src/game/Viewmodel.cpp
    src/game/Interact.cpp
    src/game/Level.cpp
    src/game/Briefing.cpp
    src/game/LevelGraph.cpp
    src/game/Enemy.cpp
    src/ui/Text.cpp
    src/ui/Menu.cpp
    src/gfx/Assets.cpp
    src/gfx/Humanoid.cpp
    src/gfx/Mesh.cpp
    src/gfx/Shader.cpp)
target_include_directories(Carnage PRIVATE src)
# Vendored single-header libs (stb_image, miniaudio); SYSTEM keeps their warnings out.
target_include_directories(Carnage SYSTEM PRIVATE third_party)
target_compile_definitions(Carnage PRIVATE CARNAGE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(Carnage PRIVATE glfw glm::glm glad_gl_core_46)

if(MSVC)
    target_compile_options(Carnage PRIVATE /W4 /permissive-)
else()
    target_compile_options(Carnage PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32)
        # Statically link the C++ runtime. We replace global operator new/delete
        # (core/Memory.cpp); a DLL libstdc++ would use its own pair and free our
        # blocks with the wrong allocator (heap corruption 0xC0000374). It also
        # stops PATH from picking another toolchain's libstdc++-6.dll.
        target_link_options(Carnage PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# --- Tests (opt-in: -DCARNAGE_BUILD_TESTS=ON; fetches Catch2) ---
option(CARNAGE_BUILD_TESTS "Build unit tests" OFF)
if(CARNAGE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(CarnageTests
        tests/test_core.cpp
        tests/test_world.cpp
        tests/test_movement.cpp
        tests/test_combat.cpp
        tests/test_level.cpp
        tests/test_prompts.cpp
        tests/test_briefing.cpp
        tests/test_hub.cpp
        tests/test_broadphase.cpp
        tests/test_wake.cpp
        src/game/World.cpp
        src/game/Movement.cpp
        src/game/Camera.cpp
        src/game/Combat.cpp
        src/game/Weapon.cpp
        src/game/Weapons.cpp
        src/game/Interact.cpp
        src/game/Level.cpp
        src/game/Briefing.cpp
        src/game/LevelGraph.cpp
        src/game/Enemy.cpp
        src/ui/Menu.cpp
        src/ui/Text.cpp)
    target_include_directories(CarnageTests PRIVATE src)
    target_compile_definitions(CarnageTests PRIVATE CARNAGE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
    target_link_libraries(CarnageTests PRIVATE glm::glm Catch2::Catch2WithMain)
    if(NOT MSVC)
        target_compile_options(CarnageTests PRIVATE -Wall -Wextra -Wpedantic)
        if(WIN32)
            target_link_options(CarnageTests PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
    enable_testing()
    add_test(NAME CarnageTests COMMAND CarnageTests)
endif()
