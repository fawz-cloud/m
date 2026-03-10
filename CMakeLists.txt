cmake_minimum_required(VERSION 3.14)
project(ZygiskSpoofer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Security Hardening Flags (applied globally BEFORE targets)
# ============================================================================
set(HARDENED_C_FLAGS   "-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fvisibility=hidden -ffunction-sections -fdata-sections")
set(HARDENED_CXX_FLAGS "${HARDENED_C_FLAGS}")
set(HARDENED_LD_FLAGS  "-Wl,-z,relro,-z,now -Wl,--gc-sections -Wl,--exclude-libs,ALL -s")

set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE}   ${HARDENED_C_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${HARDENED_CXX_FLAGS}")

# LTO for smaller binary + symbol elimination
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# ============================================================================
# ShadowHook - ByteDance Android Inline Hook
# ============================================================================
set(SHADOWHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/shadowhook")
set(SHADOWHOOK_SRC_DIR "${SHADOWHOOK_ROOT}/shadowhook/src/main/cpp")

file(GLOB_RECURSE SHADOWHOOK_SOURCES
    "${SHADOWHOOK_SRC_DIR}/*.c"
    "${SHADOWHOOK_SRC_DIR}/*.cpp"
)

add_library(shadowhook STATIC ${SHADOWHOOK_SOURCES})
target_include_directories(shadowhook PUBLIC
    "${SHADOWHOOK_SRC_DIR}/include"
    "${SHADOWHOOK_SRC_DIR}"
    "${SHADOWHOOK_SRC_DIR}/common"
    "${SHADOWHOOK_SRC_DIR}/arch/${ANDROID_ABI}"
)
target_link_libraries(shadowhook PRIVATE log dl)
# Suppress warnings in third-party code
target_compile_options(shadowhook PRIVATE -w)

# ============================================================================
# Zygisk Module Shared Library
# ============================================================================
add_library(${PROJECT_NAME} SHARED
    source/main.cpp
    source/config.cpp
    source/hook.cpp
    external/cJSON/cJSON.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON
    "${SHADOWHOOK_SRC_DIR}/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    shadowhook
    log
    dl
)

# Linker hardening flags
target_link_options(${PROJECT_NAME} PRIVATE
    ${HARDENED_LD_FLAGS}
)

# Strict warnings for our own code
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Werror=return-type -Werror=format-security
)

# Suppress warnings for cJSON (C code in C++ build)
set_source_files_properties(external/cJSON/cJSON.c PROPERTIES COMPILE_FLAGS "-w")

# Zygisk format: named {abi}.so with no prefix
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${ANDROID_ABI}"
    PREFIX ""
    SUFFIX ".so"
)
