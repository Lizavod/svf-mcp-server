cmake_minimum_required(VERSION 3.23)
project(svf-mcp-server VERSION 1.0 DESCRIPTION "MCP server for static analysis via SVF" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(MCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MCP_SSL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cpp-mcp)

target_compile_options(mcp PRIVATE "-fexceptions")


if (DEFINED LLVM_DIR)
    set(ENV{LLVM_DIR} "${LLVM_DIR}")
elseif (DEFINED ENV{LLVM_DIR})
    set(LLVM_DIR $ENV{LLVM_DIR})
else()
    message(FATAL_ERROR "Please set LLVM_DIR (e.g. -DLLVM_DIR=/path/to/LLVM)")
endif()

find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

if(LLVM_LINK_LLVM_DYLIB)
    set(llvm_libs LLVM)
else()
    llvm_map_components_to_libnames(llvm_libs bitwriter core ipo irreader instcombine instrumentation target linker analysis scalaropts support)
endif()


if(NOT DEFINED SVF_DIR AND DEFINED ENV{SVF_DIR})
    set(SVF_DIR $ENV{SVF_DIR})
endif()


find_library(Z3_LIBRARIES NAMES z3 
    HINTS ENV Z3_DIR ${Z3_DIR} 
    PATH_SUFFIXES lib lib64
)
find_path(Z3_INCLUDE_DIR NAMES z3++.h 
    HINTS ENV Z3_DIR ${Z3_DIR} 
    PATH_SUFFIXES include z3
)

if(NOT Z3_LIBRARIES OR NOT Z3_INCLUDE_DIR)
    message(FATAL_ERROR 
        "Z3 not found. Set Z3_DIR env var or pass -DZ3_DIR=/path/to/z3")
endif()

message(STATUS "Z3 library: ${Z3_LIBRARIES}")
message(STATUS "Z3 include: ${Z3_INCLUDE_DIR}")


set(Z3_FOUND TRUE CACHE BOOL "" FORCE)
set(Z3_LIBRARIES ${Z3_LIBRARIES} CACHE FILEPATH "" FORCE)
set(Z3_LIBRARY_DIR "${Z3_LIBRARIES}" CACHE PATH "" FORCE)
set(Z3_INCLUDE_DIR ${Z3_INCLUDE_DIR} CACHE PATH "" FORCE)

find_package(SVF REQUIRED CONFIG
    HINTS ${SVF_DIR} ${SVF_DIR}/Debug-build ${SVF_DIR}/Release-build
)

if(SVF_FOUND)
    message(STATUS "Found SVF: ${SVF_VERSION}")
    include_directories(SYSTEM ${SVF_INSTALL_INCLUDE_DIR})
    link_directories(${SVF_INSTALL_LIB_DIR})
else()
    message(FATAL_ERROR "SVF not found")
endif()

include_directories(SYSTEM ${Z3_INCLUDE_DIR})


set(SVF_LIB SvfLLVM SvfCore)
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(svf-mcp-server ${SOURCES})

target_include_directories(svf-mcp-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cpp-mcp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cpp-mcp/common
)


if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(svf-mcp-server PRIVATE "-fno-rtti")
endif()

target_compile_options(svf-mcp-server PRIVATE -fexceptions)

target_link_libraries(svf-mcp-server PRIVATE
    ${llvm_libs}
    ${SVF_LIB}
    ${Z3_LIBRARIES}
    mcp
)
