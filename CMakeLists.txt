cmake_minimum_required(VERSION 3.14)
project(ZygiskSpoofer LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Security Hardening Flags
# ============================================================================
set(HARDENED_C_FLAGS   "-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fvisibility=hidden -ffunction-sections -fdata-sections")
set(HARDENED_CXX_FLAGS "${HARDENED_C_FLAGS}")
set(HARDENED_LD_FLAGS  "-Wl,-z,relro,-z,now -Wl,--gc-sections -Wl,--exclude-libs,ALL -s")

set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE}   ${HARDENED_C_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${HARDENED_CXX_FLAGS}")

# LTO for smaller binary
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# ============================================================================
# ShadowHook - ByteDance Android Inline Hook
# Build matches the official CMakeLists.txt structure
# ============================================================================
set(SHADOWHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/shadowhook")
set(SHADOWHOOK_CPP  "${SHADOWHOOK_ROOT}/shadowhook/src/main/cpp")

# Determine arch subdirectory
if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(SH_ARCH "arm64")
    set(SH_ARCH_LINK_FLAGS "-Wl,-z,max-page-size=16384")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(SH_ARCH "arm")
    set(SH_ARCH_LINK_FLAGS "")
endif()

# Collect sources: core + arch (C + ASM) + common + third_party
file(GLOB SH_CORE_SRC      "${SHADOWHOOK_CPP}/*.c")
file(GLOB SH_ARCH_C_SRC    "${SHADOWHOOK_CPP}/arch/${SH_ARCH}/*.c")
file(GLOB SH_ARCH_ASM_SRC  "${SHADOWHOOK_CPP}/arch/${SH_ARCH}/*.S")
file(GLOB SH_COMMON_SRC    "${SHADOWHOOK_CPP}/common/*.c")
file(GLOB SH_XDL_SRC       "${SHADOWHOOK_CPP}/third_party/xdl/*.c")
file(GLOB SH_BSD_SRC       "${SHADOWHOOK_CPP}/third_party/bsd/*.c")
file(GLOB SH_LSS_SRC       "${SHADOWHOOK_CPP}/third_party/lss/*.c")

add_library(shadowhook STATIC
    ${SH_CORE_SRC}
    ${SH_ARCH_C_SRC}
    ${SH_ARCH_ASM_SRC}
    ${SH_COMMON_SRC}
    ${SH_XDL_SRC}
    ${SH_BSD_SRC}
    ${SH_LSS_SRC}
)

target_compile_features(shadowhook PRIVATE c_std_11)

target_include_directories(shadowhook PUBLIC
    "${SHADOWHOOK_CPP}"
    "${SHADOWHOOK_CPP}/include"
    "${SHADOWHOOK_CPP}/arch/${SH_ARCH}"
    "${SHADOWHOOK_CPP}/common"
    "${SHADOWHOOK_CPP}/third_party/xdl"
    "${SHADOWHOOK_CPP}/third_party/bsd"
    "${SHADOWHOOK_CPP}/third_party/lss"
)

target_link_libraries(shadowhook PRIVATE log dl)
target_link_options(shadowhook PRIVATE ${SH_ARCH_LINK_FLAGS})
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
    "${SHADOWHOOK_CPP}/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    shadowhook
    log
    dl
)

# Linker hardening flags
target_link_options(${PROJECT_NAME} PRIVATE
    ${HARDENED_LD_FLAGS}
    ${SH_ARCH_LINK_FLAGS}
)

# Strict warnings for our own code
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Werror=return-type -Werror=format-security
)

# Suppress warnings for cJSON
set_source_files_properties(external/cJSON/cJSON.c PROPERTIES COMPILE_FLAGS "-w")

# Zygisk format: named {abi}.so with no prefix
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${ANDROID_ABI}"
    PREFIX ""
    SUFFIX ".so"
)
