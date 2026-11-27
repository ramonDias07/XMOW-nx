# ds_dksh for Windows (clang/llvm-mingw): the gears2 tool, built against a snapshot of the patched SDK files
# (shaders-g2/sdk-min: include/rex/ui/deko3d + src/ui/deko3d/{glsl,cache_dksh}.cpp, gears2 patches 0009/0011/0044/0045)
# and the Switch SDK's thirdparty (SPIRV-Cross, uam, xxHash; sdk-nx-deko/thirdparty, same pinned commits as sdk-g2).
#   cmake -S fuente/dksh -B bld/dksh -G Ninja -DCMAKE_BUILD_TYPE=Release -DDS_TERCEROS=<thirdparty> -DDS_SDK_MIN=<sdk-min>
cmake_minimum_required(VERSION 3.20)
project(ds_dksh C CXX)

get_filename_component(DS_FUENTE "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
foreach(fuente SPIRV-Cross/spirv_glsl.cpp uam/uam_api.h xxHash/xxhash.h)
  if(NOT EXISTS "${DS_TERCEROS}/${fuente}")
    message(FATAL_ERROR "Missing ${DS_TERCEROS}/${fuente}")
  endif()
endforeach()

add_subdirectory("${DS_TERCEROS}/uam" uam EXCLUDE_FROM_ALL)
# llvm-mingw lacks glibc's strchrnul (Mesa's u_string.h uses it under _GNU_SOURCE)
target_compile_options(uamlib PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/mingw_shim.h" -UHAVE_POSIX_MEMALIGN)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${DS_TERCEROS}/SPIRV-Cross" spirv-cross EXCLUDE_FROM_ALL)

add_executable(ds_dksh
  ds_dksh.cpp
  "${DS_SDK_MIN}/src/ui/deko3d/glsl.cpp"
  "${DS_SDK_MIN}/src/ui/deko3d/cache_dksh.cpp"
  "${DS_FUENTE}/ds_shader_library.cpp")
target_include_directories(ds_dksh PRIVATE "${DS_SDK_MIN}/include" "${DS_TERCEROS}/xxHash")
target_link_libraries(ds_dksh PRIVATE uamlib spirv-cross-glsl spirv-cross-core pthread)
target_link_options(ds_dksh PRIVATE -static)
set_target_properties(ds_dksh PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Validator: reads the library and the cache the way the runtime does (gow2_shaders.cpp / patch 0045).
add_executable(ds_validar ds_validar.cpp
  "${DS_SDK_MIN}/src/ui/deko3d/glsl.cpp"
  "${DS_SDK_MIN}/src/ui/deko3d/cache_dksh.cpp"
  "${DS_FUENTE}/ds_shader_library.cpp")
target_include_directories(ds_validar PRIVATE "${DS_SDK_MIN}/include" "${DS_TERCEROS}/xxHash")
target_link_libraries(ds_validar PRIVATE spirv-cross-glsl spirv-cross-core)
target_link_options(ds_validar PRIVATE -static)
set_target_properties(ds_validar PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
