cmake_minimum_required(VERSION 3.12)

# Find dependencies (uses modernized FindLLVM.cmake to avoid warnings)
find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)
find_package(jsoncpp REQUIRED)

# --- 1. Dynamic Library Discovery Macro ---
# Order: Variable Name, Search Directory, File Pattern
macro(find_shared_lib_dynamic NAME_VAR HINT_DIR PATTERN)
    if(NOT ${NAME_VAR})
        file(GLOB CANDIDATES "${HINT_DIR}/${PATTERN}")
        if(CANDIDATES)
            list(GET CANDIDATES 0 ${NAME_VAR})
            set(${NAME_VAR} "${${NAME_VAR}}" CACHE PATH "Path to ${PATTERN}")
            message(STATUS "Found Shared Library ${PATTERN}: ${${NAME_VAR}}")
        else()
            message(FATAL_ERROR 
                " Could not automatically find ${PATTERN} in ${HINT_DIR}.\n"
                " Please specify manually: cmake -D${NAME_VAR}=/path/to/file.so ..\n"
                " Hint: On Perlmutter, ensure PrgEnv-llvm and llvm modules are loaded."
            )
        endif()
    else()
        message(STATUS "Using user-provided ${NAME_VAR}: ${${NAME_VAR}}")
    endif()
endmacro()

# Discover shared libraries (HINT_DIR comes from FindLLVM.cmake)
find_shared_lib_dynamic(LLVM_SHARED_LIB "${LLVM_LIB_DIR}" "libLLVM*.so")
find_shared_lib_dynamic(CLANG_CPP_SHARED_LIB "${LLVM_LIB_DIR}" "libclang-cpp*.so")

# --- 2. Include Configuration ---
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${CLANG_INCLUDE_DIRS})
#include_directories(SYSTEM ${CLANG_BUILTIN_INCLUDE_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g3 -O3 -fno-rtti -fno-exceptions -funwind-tables -pthread")

# --- 3. Source Definitions ---
set(CLANG_TOOL_SRC
    main.cc
    utils/utils.cc
    utils/typeTranslation.cc

    passes/checkPersistentTypes/checkPersistentTypesAction.cc
    passes/checkPersistentTypes/checkPersistentTypes.cc

    passes/rootSetup/rootSetupAction.cc
    passes/rootSetup/rootTable.cc
    passes/rootSetup/rootCollector.cc
    passes/rootSetup/rootEmitter.cc

    passes/recursePersistent/recursePersistentAction.cc
    passes/recursePersistent/specializationBuilder.cc
    passes/recursePersistent/memberPersistifier.cc
)

# --- 4. Build and Link ---
add_executable(persist-tool ${CLANG_TOOL_SRC})

# The tool lives outside clang's install tree, so tell it where the builtin headers are
get_filename_component(CLANG_RESOURCE_DIR "${CLANG_BUILTIN_INCLUDE_DIR}" DIRECTORY)
target_compile_definitions(persist-tool PRIVATE PERSIST_CLANG_RESOURCE_DIR="${CLANG_RESOURCE_DIR}")

# Strip potential whitespace from detected LDFLAGS
string(STRIP "${LLVM_LDFLAGS}" LLVM_LDFLAGS_STRIPPED)

target_link_libraries(persist-tool 
    PRIVATE
    ${CLANG_CPP_SHARED_LIB} 
    ${LLVM_SHARED_LIB} 
    ${LLVM_LDFLAGS_STRIPPED} 
    jsoncpp_lib
)

# Filesystem compatibility fix for older GCC/RHEL systems
find_library(FILESYSTEM_LIB NAMES stdc++fs)
if(FILESYSTEM_LIB)
    target_link_libraries(persist-tool PRIVATE ${FILESYSTEM_LIB})
endif()

# --- 5. Unit tests ---
# translate-type-test: table-driven test of utils::translateType (P).
add_executable(translate-type-test unittests/translateTypeTest.cc utils/utils.cc utils/typeTranslation.cc)
target_compile_definitions(translate-type-test PRIVATE
    PERSIST_CLANG_RESOURCE_DIR="${CLANG_RESOURCE_DIR}"
    PERSIST_LIB_DIR="${CMAKE_SOURCE_DIR}/../persistentLib")
target_link_libraries(translate-type-test
    PRIVATE
    ${CLANG_CPP_SHARED_LIB}
    ${LLVM_SHARED_LIB}
    ${LLVM_LDFLAGS_STRIPPED}
)
if(FILESYSTEM_LIB)
    target_link_libraries(translate-type-test PRIVATE ${FILESYSTEM_LIB})
endif()
