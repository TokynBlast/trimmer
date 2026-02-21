# ╔═══════════════════════════════════════════════════════════════════════════╗
# ║  CMakeLists.txt for Trimmer                                               ║
# ║  A high-performance, streaming, fuzzy-logic whitespace cleaner            ║
# ╚═══════════════════════════════════════════════════════════════════════════╝

cmake_minimum_required(VERSION 3.10)
project(trimmer VERSION 0.1.0 LANGUAGES C)

# ─────────────────────────────────────────────────────────────────────────────
# Build Options
# ─────────────────────────────────────────────────────────────────────────────
option(ENABLE_LTO "Enable Link Time Optimization" OFF)
option(ENABLE_NATIVE "Enable -march=native -mtune=native" OFF)
option(ENABLE_WARNINGS "Enable extra warnings" ON)
option(ENABLE_HARDENED "Enable hardened build (warnings as errors)" OFF)

# ─────────────────────────────────────────────────────────────────────────────
# Set default build type if not specified
# ─────────────────────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Choose the type of build: Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ─────────────────────────────────────────────────────────────────────────────
# C Standard
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# ─────────────────────────────────────────────────────────────────────────────
# Source Files
# ─────────────────────────────────────────────────────────────────────────────
set(SOURCE_FILES
    trim.c
)

# ─────────────────────────────────────────────────────────────────────────────
# Create Executable
# ─────────────────────────────────────────────────────────────────────────────
add_executable(trim ${SOURCE_FILES})

# ─────────────────────────────────────────────────────────────────────────────
# Find and Link pthreads
# ─────────────────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(trim PRIVATE Threads::Threads)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler Flags
# ─────────────────────────────────────────────────────────────────────────────
target_compile_definitions(trim PRIVATE
    _POSIX_C_SOURCE=200809L
    _FILE_OFFSET_BITS=64
)

# Warnings
if(ENABLE_WARNINGS)
    target_compile_options(trim PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wformat=2
    )
endif()

# Hardened build
if(ENABLE_HARDENED)
    target_compile_options(trim PRIVATE
        -Werror
        -pedantic
        -Wstrict-prototypes
        -Wmissing-prototypes
    )
endif()

# Native architecture optimization
if(ENABLE_NATIVE)
    target_compile_options(trim PRIVATE
        -march=native
        -mtune=native
    )
    message(STATUS "Native architecture optimizations enabled")
endif()

# Link Time Optimization
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set_property(TARGET trim PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    else()
        message(WARNING "LTO not supported: ${lto_error}")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Build Type Specific Flags
# ─────────────────────────────────────────────────────────────────────────────
# Debug
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")

# Release
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Release with Debug Info
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# Minimum Size Release
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")

# ─────────────────────────────────────────────────────────────────────────────
# Installation
# ─────────────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS trim
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔═══════════════════════════════════════════════════════════╗")
message(STATUS "║                  Trimmer Configuration                    ║")
message(STATUS "╚═══════════════════════════════════════════════════════════╝")
message(STATUS "  Build Type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler:        ${CMAKE_C_COMPILER}")
message(STATUS "  C Standard:      C${CMAKE_C_STANDARD}")
message(STATUS "  LTO:             ${ENABLE_LTO}")
message(STATUS "  Native Arch:     ${ENABLE_NATIVE}")
message(STATUS "  Warnings:        ${ENABLE_WARNINGS}")
message(STATUS "  Hardened:        ${ENABLE_HARDENED}")
message(STATUS "")
message(STATUS "Usage examples:")
message(STATUS "  cmake -B build                                    # Default release")
message(STATUS "  cmake -B build -DCMAKE_BUILD_TYPE=Debug           # Debug build")
message(STATUS "  cmake -B build -DENABLE_LTO=ON -DENABLE_NATIVE=ON # Optimized")
message(STATUS "  cmake --build build                               # Compile")
message(STATUS "  cmake --install build                             # Install")
message(STATUS "")
