cmake_minimum_required(VERSION 3.25)

project(MuMain)

if(MSVC)
    # MSBuild schedules up to eight projects; do not multiply that by /MP8.
    # Ninja invokes the compiler with one source per job, so /MP adds no jobs.
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        add_compile_options(/MP1)
    else()
        add_compile_options(/MP8)
    endif()
endif()

# Repository root and dependency roots.
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH REPO_ROOT)
set(MU_VENDOR_DIR "${REPO_ROOT}/vendor/third-party")
set(MU_PREBUILT_DIR "${REPO_ROOT}/vendor/prebuilt")

foreach(MU_DEPENDENCY SDL SDL_mixer RmlUi freetype lunasvg)
  if(NOT EXISTS "${MU_VENDOR_DIR}/${MU_DEPENDENCY}/CMakeLists.txt")
    message(FATAL_ERROR "Missing vendored dependency: ${MU_DEPENDENCY}")
  endif()
endforeach()

# SDL3 — static link, no tests, no install
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${MU_VENDOR_DIR}/SDL" "${CMAKE_CURRENT_BINARY_DIR}/vendor/SDL" EXCLUDE_FROM_ALL)

# SDL3_mixer — static link, MP3 + Ogg Vorbis only (matches our music assets)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDLMIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_DRMP3 ON CACHE BOOL "" FORCE)
# mpg123 is built as a dynamically loaded library — drmp3 alone is enough
# for the assets we ship and keeps everything statically linked.
set(SDLMIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
add_subdirectory("${MU_VENDOR_DIR}/SDL_mixer" "${CMAKE_CURRENT_BINARY_DIR}/vendor/SDL_mixer" EXCLUDE_FROM_ALL)

# RmlUi drives retained player UI. FreeType builds RmlUi's cached text
# geometry and atlas; the render-tape adapter still owns all GPU publication.
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${MU_VENDOR_DIR}/freetype" "${CMAKE_CURRENT_BINARY_DIR}/vendor/freetype" EXCLUDE_FROM_ALL)
if(NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
endif()

set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "" FORCE)
add_subdirectory("${MU_VENDOR_DIR}/lunasvg" "${CMAKE_CURRENT_BINARY_DIR}/vendor/lunasvg" EXCLUDE_FROM_ALL)
set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
add_subdirectory("${MU_VENDOR_DIR}/RmlUi" "${CMAKE_CURRENT_BINARY_DIR}/vendor/RmlUi" EXCLUDE_FROM_ALL)

# Determine architecture if CMAKE_SIZEOF_VOID_P isn't set
if (NOT DEFINED CMAKE_SIZEOF_VOID_P OR CMAKE_SIZEOF_VOID_P EQUAL 0)
  # Try to detect from compiler path
  if (CMAKE_CXX_COMPILER MATCHES "x64|amd64|AMD64")
    set(CMAKE_SIZEOF_VOID_P 8 CACHE INTERNAL "Pointer size" FORCE)
    message(STATUS "Detected x64 from compiler path, setting CMAKE_SIZEOF_VOID_P=8")
  elseif (CMAKE_CXX_COMPILER MATCHES "x86|i386|i686")
    set(CMAKE_SIZEOF_VOID_P 4 CACHE INTERNAL "Pointer size" FORCE)
    message(STATUS "Detected x86 from compiler path, setting CMAKE_SIZEOF_VOID_P=4")
  else()
    # Default fallback
    set(CMAKE_SIZEOF_VOID_P 4 CACHE INTERNAL "Pointer size" FORCE)
    message(WARNING "Could not detect architecture, defaulting to x86")
  endif()
endif()

# Check architecture and configure accordingly
if (MSVC)
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Configuring for 64-bit (x64) build")
  else()
    message(STATUS "Configuring for 32-bit (x86) build")
  endif()
endif()

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Build type (default RelWithDebInfo for useful symbols)" FORCE)
endif()

# ---------------------------------------------------------------------------
# Engine/game code -> static library (libMuClient); MuMain -> thin executable.
#
# All engine and game logic compiles into the MuClient static library. The
# Main executable defined further down contains only the platform entry point
# (WinMain) and links the library. Keeping the two separated lets unit-test
# binaries link the same engine code instead of recompiling individual
# translation units, and stops bootstrap-only code from leaking into the
# engine (issue #345).
# ---------------------------------------------------------------------------

# Release source is self-contained and discovers native files directly.
file(GLOB_RECURSE MU_CLIENT_APPROVED_PATHS CONFIGURE_DEPENDS
    RELATIVE "${REPO_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.rc")
set(MU_CLIENT_SOURCES ${MU_CLIENT_APPROVED_PATHS})
list(FILTER MU_CLIENT_SOURCES INCLUDE REGEX "\\.cpp$")
list(REMOVE_ITEM MU_CLIENT_SOURCES "src/client/app/platform/Main.cpp")
list(TRANSFORM MU_CLIENT_SOURCES PREPEND "${REPO_ROOT}/")

# Engine/game code as a static library. Compile- and link-time usage
# requirements (C++20, include paths, definitions, third-party libraries) are
# attached PUBLIC below so both the Main executable and any test binary that
# links MuClient inherit them automatically.
add_library(MuClient STATIC ${MU_CLIENT_SOURCES})
set(MU_CLIENT_HEADERS ${MU_CLIENT_APPROVED_PATHS})
list(FILTER MU_CLIENT_HEADERS INCLUDE REGEX "\\.h$")
list(TRANSFORM MU_CLIENT_HEADERS PREPEND "${REPO_ROOT}/")
target_sources(MuClient PRIVATE ${MU_CLIENT_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/client" PREFIX "Client"
  FILES ${MU_CLIENT_SOURCES} ${MU_CLIENT_HEADERS})
target_compile_features(MuClient PUBLIC cxx_std_20)
target_link_libraries(MuClient PUBLIC RmlUi::Core)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(MU_CLIENT_ARCH x64)
else()
  set(MU_CLIENT_ARCH x86)
endif()
if(WIN32)
  # All four client variants can run from one directory without DLL collisions.
  set(MU_MANAGED_LIBRARY_FILENAME "MUnique.Client.Library-${MU_CLIENT_ARCH}-$<CONFIG>.dll")
  target_compile_definitions(MuClient PRIVATE
    MU_MANAGED_LIBRARY_FILENAME="${MU_MANAGED_LIBRARY_FILENAME}")
endif()

# Stable external dependencies only. CMake generates cmake_pch.hxx in the build
# tree; authored buckets keep explicit includes for their own dependencies.
# PRIVATE keeps this compiler artifact local to MuClient's compile settings.
target_compile_definitions(MuClient PRIVATE NOMINMAX)
target_precompile_headers(MuClient PRIVATE
  <algorithm>
  <array>
  <atomic>
  <bit>
  <bitset>
  <cassert>
  <cctype>
  <cerrno>
  <charconv>
  <chrono>
  <climits>
  <clocale>
  <cmath>
  <codecvt>
  <condition_variable>
  <csetjmp>
  <cstddef>
  <cstdint>
  <cstdio>
  <cstdlib>
  <cstring>
  <ctime>
  <cwchar>
  <cwctype>
  <deque>
  <exception>
  <execution>
  <filesystem>
  <fstream>
  <functional>
  <initializer_list>
  <iostream>
  <iterator>
  <limits>
  <list>
  <locale>
  <map>
  <memory>
  <mutex>
  <new>
  <numbers>
  <numeric>
  <optional>
  <queue>
  <random>
  <regex>
  <set>
  <span>
  <sstream>
  <stack>
  <stdexcept>
  <string>
  <string_view>
  <thread>
  <tuple>
  <type_traits>
  <unordered_map>
  <unordered_set>
  <utility>
  <variant>
  <vector>
  <ctype.h>
  <math.h>
  <setjmp.h>
  <time.h>
  <SDL3/SDL.h>
  <SDL3_mixer/SDL_mixer.h>
  <RmlUi/Core.h>
  <RmlUi/Core/BaseXMLParser.h>
  <RmlUi/Core/ComputedValues.h>
  <RmlUi/Core/Context.h>
  <RmlUi/Core/Element.h>
  <RmlUi/Core/ElementDocument.h>
  <RmlUi/Core/ElementUtilities.h>
  <RmlUi/Core/Elements/ElementFormControl.h>
  <RmlUi/Core/Elements/ElementFormControlInput.h>
  <RmlUi/Core/Elements/ElementFormControlSelect.h>
  <RmlUi/Core/Event.h>
  <RmlUi/Core/EventListener.h>
  <RmlUi/Core/Geometry.h>
  <RmlUi/Core/Input.h>
  <RmlUi/Core/Mesh.h>
  <RmlUi/Core/MeshUtilities.h>
  <RmlUi/Core/PropertyDefinition.h>
  <RmlUi/Core/RenderManager.h>
  <RmlUi/Core/Spritesheet.h>
  <RmlUi/Core/StreamMemory.h>
  <RmlUi/Core/StringUtilities.h>
)


# The in-game shop downloader uses libcurl off Windows (issue #462); on Windows
# it uses the OS WinINet/urlmon stack and needs no extra dependency here.
if(NOT WIN32)
  find_package(CURL REQUIRED)
  target_link_libraries(MuClient PUBLIC CURL::libcurl)
endif()

# The engine relies on self-registering globals whose constructors get
# silently dropped if the linker only pulls archive members on demand, so
# every consumer must link MuClient with WHOLE_ARCHIVE. Rather than make each
# consumer remember that, MuClient::MuClient is an interface wrapper that
# carries the WHOLE_ARCHIVE requirement (and, transitively, all of MuClient's
# public compile/link properties). Executables and unit tests just link
# MuClient::MuClient; link the raw MuClient target directly only when an
# on-demand link is genuinely wanted.
add_library(MuClient_whole INTERFACE)
target_link_libraries(MuClient_whole INTERFACE $<LINK_LIBRARY:WHOLE_ARCHIVE,MuClient>)
add_library(MuClient::MuClient ALIAS MuClient_whole)

# Windows and Linux entry points share the approved bootstrap bucket.
if (WIN32)
  set(MU_APP_ENTRY "client/app/platform/Main.cpp")
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "iOS is not a supported build target yet: it needs an "
    "OpenGL ES/Metal renderer port and a touch UI, and distribution is App "
    "Store restricted. Amend the approved platform bucket before adding support.")
elseif (APPLE)
  message(FATAL_ERROR "macOS bootstrap is not implemented in the approved platform bucket.")
elseif (ANDROID)
  message(FATAL_ERROR "Android is not a supported build target yet: it needs "
    "an OpenGL ES renderer port, and its entry point is android_main via "
    "native_app_glue built as a shared library, not an executable. When that "
    "work starts, amend the approved platform bucket and replace this branch.")
elseif (UNIX)
  set(MU_APP_ENTRY "client/app/platform/Main.cpp")
else()
  message(FATAL_ERROR "No executable entry point defined for this platform "
    "(CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}). Amend the approved platform bucket.")
endif()

add_executable(Main
  "${CMAKE_CURRENT_SOURCE_DIR}/${MU_APP_ENTRY}"
)
set(MU_MAIN_OUTPUT_NAME "Main" CACHE STRING
  "Filename stem for the client executable")
set_target_properties(Main PROPERTIES OUTPUT_NAME "${MU_MAIN_OUTPUT_NAME}")
target_compile_features(Main PUBLIC cxx_std_20)
target_link_libraries(Main PRIVATE MuClient::MuClient)

if(MSVC)
  # Preserve symbols in optimized builds for the Visual Studio memory profiler.
  set_target_properties(MuClient Main PROPERTIES
    MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
  target_link_options(Main PRIVATE /DEBUG:FULL
    $<$<CONFIG:Release>:/OPT:REF;/OPT:ICF>)
endif()

# Multi-session x86 builds retain the legacy 32-bit ABI but need more than the
# default 2 GiB user virtual-address ceiling. On 64-bit Windows this flag gives
# the process the supported 4 GiB x86 address space; allocation failures remain
# handled transactionally by the session runtime.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  target_link_options(Main PRIVATE /LARGEADDRESSAWARE)
endif()

if (NOT MSVC)
  set(MU_MAIN_MAP_FILE "${CMAKE_CURRENT_BINARY_DIR}/Main.map")
  target_link_options(Main PRIVATE "-Wl,-Map,${MU_MAIN_MAP_FILE}")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Build as a Windows GUI application (no console window) and let CMake
  # add the default system libraries like kernel32, user32, gdi32, etc.
  set_target_properties(Main PROPERTIES
    WIN32_EXECUTABLE TRUE
  )

  enable_language(RC)
  target_sources(Main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/client/app/platform/resource.rc"
  )
endif()

# Copy assets directory next to executable. Platform-agnostic: any build
# that produces the Main executable needs the assets alongside it.
# The custom command's stamp file depends on every file under
# ${ASSETS_DIR}, so the copy only runs when at least one asset is newer
# than the stamp -- unchanged builds skip the copy entirely, edited
# assets re-trigger it. CONFIGURE_DEPENDS makes CMake re-evaluate the
# glob at build time so newly-added asset files are picked up without a
# manual reconfigure.
set(ASSETS_DIR "${REPO_ROOT}/assets/runtime")
if(NOT EXISTS "${ASSETS_DIR}/Data/UI/PC/ui_assets.rcss")
  message(FATAL_ERROR "Missing generated UI skin linker: Data/UI/PC/ui_assets.rcss")
endif()
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${ASSETS_DIR}/*")
# $<CONFIG> in the stamp name keeps multi-config generators (Visual
# Studio, Xcode) honest: each configuration's output directory needs
# its own stamp, otherwise switching from Debug to Release would see
# the Debug stamp and skip the copy, leaving the Release dir empty.
set(ASSETS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.assets_copied_$<CONFIG>.stamp")
add_custom_command(
  OUTPUT "${ASSETS_STAMP}"
  COMMAND ${CMAKE_COMMAND} -E echo "=== Copying assets from ${ASSETS_DIR} to $<TARGET_FILE_DIR:Main>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${ASSETS_DIR}"
          "$<TARGET_FILE_DIR:Main>"
  COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_STAMP}"
  DEPENDS ${ASSET_FILES}
  COMMENT "Copying game assets (sources changed)"
  VERBATIM
)
add_custom_target(CopyAssets DEPENDS "${ASSETS_STAMP}")
add_dependencies(Main CopyAssets)

# Seed configs/config.ini only when missing; migrate a legacy root config first.
# The bulk asset copy ships configs/config.ini.template on every build.
# the first time and then leaves the user's edited copy alone forever.
add_custom_command(TARGET Main POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    "-DINPUT=${ASSETS_DIR}/configs/config.ini.template"
    "-DOUTPUT=$<TARGET_FILE_DIR:Main>/configs/config.ini"
    "-P" "${REPO_ROOT}/cmake/SeedConfigIfMissing.cmake"
  VERBATIM
)

# Skeleton Linux-native linkage. This intentionally depends only on
# ubiquitous system libraries so we can focus on getting the project to
# compile; more platform-specific adjustments will follow as we iterate.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_library(TURBOJPEG_LIBRARY NAMES turbojpeg REQUIRED)
  target_link_libraries(MuClient PUBLIC
    pthread
    dl
    m
    ${TURBOJPEG_LIBRARY}
  )
endif()

# --- .NET / NuGet infrastructure for ClientLibrary ---------------------------
# A Linux target needs the native Linux dotnet to Native-AOT-compile the
# linux-x64 .so (a Windows dotnet.exe cannot cross-OS AOT). Everything else
# (Windows/MinGW targets, including from WSL) prefers dotnet.exe so the win-*
# AOT build works. Pick accordingly.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_program(DOTNET_EXECUTABLE dotnet)
  if (NOT DOTNET_EXECUTABLE)
    find_program(DOTNET_EXECUTABLE dotnet.exe)
  endif()
else()
  find_program(DOTNET_EXECUTABLE dotnet.exe)
  if (NOT DOTNET_EXECUTABLE)
    find_program(DOTNET_EXECUTABLE dotnet)
  endif()
endif()

# Helper macro: convert a path to Windows-native format when using
# Windows dotnet.exe from WSL. On all other platforms this is a no-op.
function(mu_native_path input_path output_var)
  if (DOTNET_EXECUTABLE MATCHES "\\.exe$" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    execute_process(
      COMMAND wslpath -w "${input_path}"
      OUTPUT_VARIABLE native_path
      RESULT_VARIABLE rc
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (rc EQUAL 0)
      set(${output_var} "${native_path}" PARENT_SCOPE)
    else()
      message(FATAL_ERROR "wslpath failed for '${input_path}'. Ensure wslpath is available.")
    endif()
  else()
    set(${output_var} "${input_path}" PARENT_SCOPE)
  endif()
endfunction()

if(DOTNET_EXECUTABLE)
  execute_process(COMMAND "${DOTNET_EXECUTABLE}" --list-sdks
    OUTPUT_VARIABLE MU_DOTNET_SDKS RESULT_VARIABLE MU_DOTNET_SDK_RESULT)
  if(NOT MU_DOTNET_SDK_RESULT EQUAL 0 OR NOT MU_DOTNET_SDKS MATCHES "(^|\n)10\\.")
    message(FATAL_ERROR
      "The .NET 10 SDK is required. Set DOTNET_EXECUTABLE to a dotnet installation containing SDK 10. Found: ${DOTNET_EXECUTABLE}")
  endif()
endif()

# Keep downloaded packages with all other generated build content.
set(MU_NUGET_CACHE_DIR "${CMAKE_SOURCE_DIR}/build/packages/nuget" CACHE PATH "NuGet package cache directory")
file(MAKE_DIRECTORY "${MU_NUGET_CACHE_DIR}")

# Release builds consume generated source directly. This keeps the package
# independent from project-only code generators and shader compiler tools.
set(MU_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
foreach(MU_GENERATED_REQUIRED
    "${MU_GENERATED_DIR}/Network/Server/wire_sizes.generated.h"
    "${MU_GENERATED_DIR}/App/Render/Shaders/RenderTapeShaderArtifacts.generated.h"
    "${MU_GENERATED_DIR}/I18N/All.h"
    "${MU_GENERATED_DIR}/I18N/All.cpp")
  if(NOT EXISTS "${MU_GENERATED_REQUIRED}")
    message(FATAL_ERROR "Missing generated release source: ${MU_GENERATED_REQUIRED}")
  endif()
endforeach()
file(GLOB MU_I18N_SOURCES CONFIGURE_DEPENDS "${MU_GENERATED_DIR}/I18N/*.cpp")
target_sources(MuClient PRIVATE ${MU_I18N_SOURCES})
source_group(TREE "${MU_GENERATED_DIR}" PREFIX "Generated" FILES ${MU_I18N_SOURCES})

target_include_directories(MuClient PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/client"
  "${MU_PREBUILT_DIR}/include"
  "${MU_PREBUILT_DIR}/netcore/includes"
  "${MU_GENERATED_DIR}"
  "${MU_VENDOR_DIR}"
  # stb_truetype backs the portable GDI text rasterizer (issue #462).
  "${MU_VENDOR_DIR}/stb"
)

# Basic compile definitions, roughly mirroring the original MSVC project
# (fine-tuning per configuration can be done later if needed.)
target_compile_definitions(MuClient PUBLIC
  _LANGUAGE_FOREIGN
  _LANGUAGE_ENG
  UNICODE
  $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:_USE_32BIT_TIME_T>  # Use 32-bit time_t only for 32-bit builds
  $<$<CONFIG:Debug>:_DEBUG;_FOREIGN_DEBUG>
  $<$<CONFIG:Release>:NDEBUG;_FOREIGN_NDEBUG;LDS_PATCH_GLOBAL_100520>
)

if (MSVC)
  target_compile_definitions(MuClient PUBLIC
    WIN32
    _WINDOWS
  )

  # Link against Windows system libraries and local static libs
  # Use architecture-specific library directories
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(LIB_ARCH_DIR "x64")
  else()
    set(LIB_ARCH_DIR "x86")
  endif()

  target_link_directories(MuClient PUBLIC
    "${MU_PREBUILT_DIR}/lib/${LIB_ARCH_DIR}"
    "${MU_PREBUILT_DIR}/lib"  # Fallback for arch-independent libs
  )

  target_link_libraries(MuClient PUBLIC
    imm32
    vfw32
    dsound
    dxguid
    winmm
    ws2_32
    version
    shlwapi
    crypt32
    wininet
    urlmon
    turbojpeg-static
  )

  # SDL_mixer's MP3/Vorbis decoders are static; no legacy audio DLLs are needed.
endif()

# .NET Client Library (platform-agnostic, requires dotnet SDK)
# DOTNET_EXECUTABLE, mu_native_path() and MU_NUGET_CACHE_DIR are already
# resolved above (shared with the wire-size codegen block).
if (DOTNET_EXECUTABLE)
  # Native AOT cannot cross-compile across OS boundaries, so gate the managed
  # library separately rather than clearing DOTNET_EXECUTABLE altogether.
  set(DOTNET_CAN_BUILD_CLIENT_LIBRARY TRUE)
  if (NOT DOTNET_EXECUTABLE MATCHES "\\.exe$" AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(WARNING "Found Linux dotnet but target is Windows. "
      "Cross-OS Native AOT is not supported. MUnique.Client.Library.dll will NOT be built. "
      "Install the Windows .NET SDK or use WSL interop (dotnet.exe) to enable.")
    set(DOTNET_CAN_BUILD_CLIENT_LIBRARY FALSE)
  endif()
endif()
if (DOTNET_EXECUTABLE)
  message(STATUS "Found .NET SDK: ${DOTNET_EXECUTABLE}")

  # 1. Define the output path. Native AOT emits a platform-native shared
  # library: .dll on Windows, .so on Linux (the runtime client dlopen's it by
  # this name).
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DOTNET_LIB_NAME "MUnique.Client.Library.so")
  else()
    set(DOTNET_LIB_NAME "MUnique.Client.Library.dll")
  endif()
  if(WIN32)
    set(DOTNET_RUNTIME_NAME "${MU_MANAGED_LIBRARY_FILENAME}")
  else()
    set(DOTNET_RUNTIME_NAME "${DOTNET_LIB_NAME}")
  endif()
  set(DOTNET_DLL_PATH "${CMAKE_CURRENT_BINARY_DIR}/ClientLibrary/$<CONFIG>/${DOTNET_RUNTIME_NAME}")
  set(DOTNET_PROJ "${CMAKE_CURRENT_SOURCE_DIR}/client-library/MUnique.Client.Library.csproj")

  # Create temp directories for Native AOT build.
  set(DOTNET_TEMP_OUTPUT "${CMAKE_BINARY_DIR}/dotnet_out/$<CONFIG>")
  set(DOTNET_TEMP_DIR "${CMAKE_BINARY_DIR}/.dotnet_temp")
  set(DOTNET_NUGET_DIR "${MU_NUGET_CACHE_DIR}")
  file(MAKE_DIRECTORY "${DOTNET_TEMP_DIR}")

  # Convert paths to Windows-native format for MSBuild (no-op outside WSL).
  mu_native_path("${DOTNET_PROJ}" DOTNET_PROJ_NATIVE)
  mu_native_path("${DOTNET_TEMP_OUTPUT}" DOTNET_TEMP_OUTPUT_NATIVE)
  mu_native_path("${DOTNET_NUGET_DIR}" DOTNET_NUGET_DIR_NATIVE)
  mu_native_path("${DOTNET_TEMP_DIR}" DOTNET_TEMP_DIR_NATIVE)

  # 2. Tell CMake to watch ALL .cs files in the ClientLibrary folder
  file(GLOB_RECURSE DOTNET_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/client-library/*.cs")
  file(GLOB DOTNET_GENERATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/client-library/*.xslt")

  # 3. Build to temp directory with Native AOT, then copy to build and bin directories
  # Override environment variables to avoid issues with umlauts in Windows username
  # Determine the runtime identifier from the target OS and pointer width. Native
  # AOT has no linux-x86 target, so 32-bit is Windows-only.
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DOTNET_RID "linux-x64")
    set(DOTNET_PLATFORM "x64")
    # The PreBuild XSLT step regenerates files using Windows-style backslash
    # paths and corrupts them on Linux; the generated sources are committed, so
    # skip it (ci=true) and use the checked-in output.
    set(DOTNET_EXTRA_ARGS "-p:ci=true")
    message(STATUS ".NET Client Library will build for linux-x64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DOTNET_RID "win-x64")
    set(DOTNET_PLATFORM "x64")
    set(DOTNET_EXTRA_ARGS "")
    message(STATUS ".NET Client Library will build for win-x64 (CMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P})")
  else()
    set(DOTNET_RID "win-x86")
    set(DOTNET_PLATFORM "x86")
    set(DOTNET_EXTRA_ARGS "")
    message(STATUS ".NET Client Library will build for win-x86 (CMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P})")
  endif()

  if (DOTNET_CAN_BUILD_CLIENT_LIBRARY)
    set(DOTNET_OUTPUTS "${DOTNET_DLL_PATH}")
    set(DOTNET_SYMBOL_COPY)
    if(WIN32)
      list(APPEND DOTNET_OUTPUTS "${DOTNET_TEMP_OUTPUT}/MUnique.Client.Library.pdb")
      set(DOTNET_SYMBOL_COPY COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DOTNET_TEMP_OUTPUT}/MUnique.Client.Library.pdb"
        "$<TARGET_FILE_DIR:Main>/MUnique.Client.Library-${MU_CLIENT_ARCH}-$<CONFIG>.pdb")
    endif()
    add_custom_command(
            OUTPUT ${DOTNET_OUTPUTS}
            COMMAND ${CMAKE_COMMAND} -E echo "--- C# Changes Detected: Rebuilding Client Library ---"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_CURRENT_BINARY_DIR}/ClientLibrary/$<CONFIG>"
            COMMAND ${CMAKE_COMMAND} -E env
                    "NUGET_PACKAGES=${DOTNET_NUGET_DIR_NATIVE}"
                    "DOTNET_CLI_HOME=${DOTNET_TEMP_DIR_NATIVE}"
                    "TEMP=${DOTNET_TEMP_DIR_NATIVE}"
                    "TMP=${DOTNET_TEMP_DIR_NATIVE}"
                    "${DOTNET_EXECUTABLE}" publish "${DOTNET_PROJ_NATIVE}" -c $<CONFIG> -r ${DOTNET_RID} -p:PlatformTarget=${DOTNET_PLATFORM} ${DOTNET_EXTRA_ARGS} -o "${DOTNET_TEMP_OUTPUT_NATIVE}" --nologo
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DOTNET_TEMP_OUTPUT}/${DOTNET_LIB_NAME}" "${DOTNET_DLL_PATH}"
            DEPENDS "${DOTNET_PROJ}" ${DOTNET_SOURCES} ${DOTNET_GENERATOR_SOURCES}
            COMMENT "Checking for .NET Client Library updates..."
            VERBATIM
    )

    # Stage on every Main build: a C#-only edit does not relink the native EXE.
    add_custom_target(ClientLibrary
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Main>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DOTNET_DLL_PATH}" "$<TARGET_FILE_DIR:Main>"
      ${DOTNET_SYMBOL_COPY}
      DEPENDS ${DOTNET_OUTPUTS}
      VERBATIM)
    # Build the native networking library before compiling or linking the client.
    add_dependencies(MuClient ClientLibrary)
    add_dependencies(Main ClientLibrary)
    set_target_properties(ClientLibrary PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
else()
  message(WARNING ".NET SDK not found. MUnique.Client.Library.dll will NOT be built. "
    "The game will run but cannot connect to the server. Install .NET SDK 10.0+ to enable.")
endif()

# MinGW (32-bit or 64-bit) cross-toolchains: link required Windows libraries.
# For turbojpeg and wzAudio we link against the logically named libraries,
# which must be provided as MinGW-compatible .a import libraries.
if (CMAKE_CXX_COMPILER MATCHES "mingw32" OR CMAKE_CXX_COMPILER MATCHES "w64-mingw32")
  target_link_directories(MuClient PUBLIC
    "${MU_PREBUILT_DIR}/lib"
  )

  set(MU_TURBOJPEG_STATIC_LIB "" CACHE FILEPATH "Path to MinGW-w64 static libturbojpeg.a")
  if (MU_TURBOJPEG_STATIC_LIB STREQUAL "" OR MU_TURBOJPEG_STATIC_LIB MATCHES "-NOTFOUND$")
    unset(MU_TURBOJPEG_STATIC_LIB CACHE)
  endif()
  if (NOT MU_TURBOJPEG_STATIC_LIB)
    find_library(MU_TURBOJPEG_STATIC_LIB
      NAMES turbojpeg
      PATHS
        "${MU_PREBUILT_DIR}/lib"
      NO_CMAKE_FIND_ROOT_PATH
    )
  endif()
  if (NOT MU_TURBOJPEG_STATIC_LIB)
    set(_MU_OLD_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(_MU_TURBOJPEG_SEARCH_PATHS "")
    foreach(_mu_root IN LISTS CMAKE_FIND_ROOT_PATH)
      list(APPEND _MU_TURBOJPEG_SEARCH_PATHS "${_mu_root}/lib" "${_mu_root}/usr/lib")
    endforeach()
    list(APPEND _MU_TURBOJPEG_SEARCH_PATHS "/usr/i686-w64-mingw32/lib" "/usr/x86_64-w64-mingw32/lib")
    find_library(MU_TURBOJPEG_STATIC_LIB
      NAMES turbojpeg
      PATHS ${_MU_TURBOJPEG_SEARCH_PATHS}
      NO_CMAKE_FIND_ROOT_PATH
    )
    unset(_MU_TURBOJPEG_SEARCH_PATHS)
    unset(_mu_root)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_MU_OLD_FIND_LIBRARY_SUFFIXES}")
    unset(_MU_OLD_FIND_LIBRARY_SUFFIXES)
  endif()
  if (NOT MU_TURBOJPEG_STATIC_LIB OR MU_TURBOJPEG_STATIC_LIB MATCHES "\\.dll\\.a$")
    message(FATAL_ERROR "Static libturbojpeg not found (libturbojpeg.a) or CMake picked an import library (.dll.a): '${MU_TURBOJPEG_STATIC_LIB}'. Install/build a MinGW-w64 static libjpeg-turbo to avoid shipping libturbojpeg.dll, or set MU_TURBOJPEG_STATIC_LIB to the full path of libturbojpeg.a.")
  endif()

  target_link_libraries(MuClient PUBLIC
    imm32
    vfw32
    dsound
    dxguid
    winmm
    ws2_32
    version
    shlwapi
    crypt32
    wininet
    urlmon
    gdi32
    user32
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ${MU_TURBOJPEG_STATIC_LIB}
  )
endif()

# Link SDL3 + SDL3_mixer (audio) on all platforms.
target_link_libraries(MuClient PUBLIC
  SDL3::SDL3
  SDL3_mixer::SDL3_mixer
)
if(WIN32)
  target_link_libraries(MuClient PUBLIC pdh shell32)
endif()

# Source-owned icons moved with resource.rc.
if(WIN32)
  target_include_directories(Main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/client/app/platform")
endif()
