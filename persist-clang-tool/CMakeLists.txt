cmake_minimum_required(VERSION 3.12)
project(clang-tool)

# --- 0. Terminal Help Message ---
set(HELP_MSG "
Usage: cmake [options] ..

Options:
  -DHELP=ON                     Print this help message and exit.
  -DCLANG_VER=<num>             Specify Clang version (e.g., 20).
                                Defaults to auto-detection via 'clang++'.
  -DCLANG_BUILTIN_INCLUDE_DIR=  Manually set the path to Clang builtin headers.
                                Defaults to auto-detection via clang resource-dir.
  -DLLVM_SHARED_LIB=<path>      Manually specify the libLLVM.so file.
  -DCLANG_CPP_SHARED_LIB=<path> Manually specify the libclang-cpp.so file.
  -DLLVM_LDFLAGS=\"<flags>\"      Manually provide linker flags.

Examples:
  cmake -DHELP=ON ..
  cmake -DCLANG_VER=20 ..
")

if(HELP)
    message("${HELP_MSG}")
    return()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/)

# --- 1. Clang Version Logic ---
set(CLANG_VER "" CACHE STRING "Clang version number")

if (NOT CLANG_VER)
    execute_process(
        COMMAND bash -c "clang++ --version | grep -Eo 'clang version [0-9]+' | awk '{print $3}' | head -n1"
        OUTPUT_VARIABLE CLANG_VER
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (CLANG_VER)
        message(STATUS "Auto-detected Clang version: ${CLANG_VER}")
    else()
        message(FATAL_ERROR "Failed to detect clang version. Pass -DCLANG_VER=<version> or -DHELP=ON.")
    endif()
endif()

# --- 2. Clang Builtin Include Detection ---
if(NOT CLANG_BUILTIN_INCLUDE_DIR)
    message(STATUS "Detecting Clang builtin include directory...")
    execute_process(
        COMMAND clang++ -print-resource-dir
        OUTPUT_VARIABLE CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(CLANG_RESOURCE_DIR)
        set(DETECTED_INCLUDE_DIR "${CLANG_RESOURCE_DIR}/include")
    endif()
endif()

if(NOT CLANG_BUILTIN_INCLUDE_DIR AND NOT DETECTED_INCLUDE_DIR)
    message(FATAL_ERROR "Could not automatically detect CLANG_BUILTIN_INCLUDE_DIR. Specify manually with -DCLANG_BUILTIN_INCLUDE_DIR=<path>")
endif()

if(NOT CLANG_BUILTIN_INCLUDE_DIR)
    set(CLANG_BUILTIN_INCLUDE_DIR "${DETECTED_INCLUDE_DIR}" CACHE PATH "Path to clang builtin headers")
endif()

if(NOT EXISTS "${CLANG_BUILTIN_INCLUDE_DIR}")
    message(FATAL_ERROR "The path does not exist: ${CLANG_BUILTIN_INCLUDE_DIR}")
endif()

message(STATUS "Using Clang builtin include path: ${CLANG_BUILTIN_INCLUDE_DIR}")

# --- 3. Configuration & Subdirectories ---
file(WRITE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/builtInInclude.path" "${CLANG_BUILTIN_INCLUDE_DIR}\n")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
add_subdirectory(src)
