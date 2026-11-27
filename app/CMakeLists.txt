# xmow - X-Men Origins: Wolverine (Xbox 360, retail disc, no title update) on the Nintendo Switch.
# App layer from gears2-nx (VasilkovND, GPL-3) adapted to Wolverine by XMOW-nx. See docs/referencias.md.
#
# The recompiled game code is generated from your own default.xex by tools/codegen.ps1. Point GOW2_CODEGEN_DIR at
# its output. At configure time tools/prepare_tree.py mirrors generated/default/ (hard links or copies, not symlinks)
# with a Switch PCH (tools/make_nx_pch.py). tools/build.ps1 does all of it.

cmake_minimum_required(VERSION 3.25)
project(xmow LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 0.1.26 (pesquisa-fps-picos.md item 8, from the renut ultimate base app/CMakeLists.txt): -mearly-ra=none for everything
# compiled for the console, SDK included (set here, before gow2_switch.cmake adds the SDK, so its targets get it too) and
# on the link. devkitA64 GCC 16.1 runs an AArch64 "early register allocation" pass at -O2 and above (-mearly-ra=all); in
# Peggle 2's console build it dropped table lookups of the NEON vperm helper (rex/ppc/intrinsics.h, two-register tbl)
# and large textures came out as garbage on the console only. A correctness guard, not a speed flag.
# XMOW_EARLY_RA=ON puts the compiler's default back.
option(XMOW_EARLY_RA "Leave GCC's AArch64 early register allocation pass on (miscompiles, see above)" OFF)
if(NOT XMOW_EARLY_RA)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-mearly-ra=none>)
    add_link_options(-mearly-ra=none)
    message(STATUS "xmow: -mearly-ra=none")
endif()
# 0.1.26: -fno-math-errno on the recompiled units (cmake/gow2_switch.cmake) and the app's own files (below). Exact: sqrt
# and friends become one instruction instead of a check plus a possible libm call that only sets errno, which the
# recompiled code never reads (no -ffinite-math-only, no -fno-trapping-math: NaN/Inf and flush-mode semantics unchanged).
option(XMOW_MATH_ERRNO_OFF "-fno-math-errno on the recompiled code and the app" ON)

include(cmake/gow2_switch.cmake)

if(NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "xmow: only the deko3d build exists (-DREXGLUE_SWITCH_DEKO3D=ON)")
endif()

# Left out of the build on purpose (hard-coded Gears of War 2 guest addresses; the reasons and the TODO lists are in
# the gears2-nx tree): gow2/src/gow2_perf_hooks.cpp (and with it the [esperas] report and the native LZX / mixer / memcpy
# hooks) and gow2/src/gow2_memory_hooks.cpp (kept out of src/ so that patch_inline_calls does not read their hooks). src/gow2_game_patches.cpp was removed (it only existed for the Gears patches
# the codegen would have inserted, which tools/codegen.ps1 does not run).
add_executable(xmow
    src/main.cpp
    src/gow2_gpu.cpp
    src/gow2_shaders.cpp
    src/gow2_guards.cpp
    src/gow2_cpu.cpp
    src/gow2_menu.cpp
    # XmowGamePatch(id): the switches of the game patches tools/codegen/patch_xmow_jogo.py puts in the code.
    src/xmow_game_patches.cpp
    # Official GPU boost (xmow_gpu_boost cvars; a pthread applies the apm profile).
    src/xmow_gpu_boost.cpp
    # Direct3D's GPU spin and occlusion GetData hooks (xmow_spin_cede, xmow_consulta_inmediata).
    src/xmow_perf_hooks.cpp
    # 0.1.33: render thread CPU cuts (native RHI shader constant setters, material parameter replay) and their report.
    src/xmow_cpu33.cpp
    src/xmow_cpu33_params.cpp
    src/xmow_cpu33_material.cpp
    # Internal resolution (UE3 ScreenPercentage) and shadow cut on FSystemSettings after LoadFromIni
    # (xmow_escala_interna, xmow_sombras).
    src/xmow_resolucao_interna.cpp
    src/xmow_bench.cpp
    # The shader library reader, shared with the PC tools (shaders/).
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ds_shader_library.cpp
)

gow2_setup_target(xmow)
# A variant with the mtmsrd regions as the codegen's process-wide mutex by default (src/gow2_cpu.cpp), to compare.
option(GOW2_MSR_GLOBAL "The mtmsrd regions as one global mutex by default" OFF)
if(GOW2_MSR_GLOBAL)
    set_source_files_properties(src/gow2_cpu.cpp PROPERTIES COMPILE_DEFINITIONS GOW2_MSR_GLOBAL_DEFAULT=1)
    message(STATUS "xmow: mtmsrd regions as one global mutex by default")
endif()
rexglue_switch_add_deko3d(xmow)
target_compile_definitions(xmow PRIVATE GOW2_DEKO3D=1)
if(XMOW_MATH_ERRNO_OFF)
    target_compile_options(xmow PRIVATE -fno-math-errno)
endif()
target_include_directories(xmow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${REXSDK_DIR}/thirdparty/xxHash)

# The shader create hooks need the two names codegen/names.toml gives the XDK Create*Shader.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml" GOW2_NAMES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml")
if(GOW2_NAMES MATCHES "\nname = \"xmow_D3DDevice_CreateVertexShader\"" AND
   GOW2_NAMES MATCHES "\nname = \"xmow_D3DDevice_CreatePixelShader\"")
    target_sources(xmow PRIVATE src/gow2_shader_dump.cpp)
    message(STATUS "xmow: shader create hooks on")
else()
    message(STATUS "xmow: shader create hooks off (codegen/names.toml has no Create*Shader names)")
endif()

# The version (NACP and the first lines of the log) and what the build was made from.
set(XMOW_NRO_VERSION "0.1.34" CACHE STRING "NACP version")
set(GOW2_VERSION "${XMOW_NRO_VERSION}")
# This folder is not a git repository: the build is identified by the SDK base it was made on.
set(GOW2_COMMIT "sdk-g2.f961303+0001-0050+1001+1002+1003+1004+1005+1006+1007-1039, codegen loteA+loteB+loteC (gerador fork LoS)")
target_compile_definitions(xmow PRIVATE GOW2_VERSION="${GOW2_VERSION}" GOW2_COMMIT="${GOW2_COMMIT}")

# Nintendo Switch: xmow.nro. On the SD card: sdmc:/switch/XMOW/XMOW.nro, with game_root/ beside it
# (src/gow2_app.h finds it there). Icon: icon.jpg (256x256 baseline JPEG, game cover cropped).
if(REXGLUE_PLATFORM_SWITCH)
    rexglue_switch_add_nro(xmow
        NAME "X-Men Origins: Wolverine"
        AUTHOR "XMOW-nx / ReXGlue"
        VERSION "${GOW2_VERSION}"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
endif()
