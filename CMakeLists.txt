cmake_minimum_required(VERSION 3.23)
project(svf-mcp-server VERSION 1.0 DESCRIPTION "MCP server for static analysis via SVF" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 1. Подключение cpp-mcp (вендоринг)
# ============================================================
set(MCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MCP_SSL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cpp-mcp)

target_compile_options(mcp PRIVATE "-fexceptions")

# ============================================================
# 2. Настройка LLVM
# ============================================================
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

# ============================================================
# 3. Настройка SVF и Z3
# ============================================================
if(NOT DEFINED SVF_DIR AND DEFINED ENV{SVF_DIR})
    set(SVF_DIR $ENV{SVF_DIR})
endif()

# КРИТИЧЕСКИ ВАЖНО: явно задаём пути к Z3 ПЕРЕД вызовом find_package(SVF),
# потому что SVF-шный FindZ3.cmake требует эти переменные
set(Z3_FOUND TRUE CACHE BOOL "" FORCE)
set(Z3_LIBRARIES z3 CACHE STRING "" FORCE)
set(Z3_LIBRARY_DIR "/usr/lib/x86_64-linux-gnu" CACHE PATH "" FORCE)
set(Z3_INCLUDE_DIR "/usr/include" CACHE PATH "" FORCE)

find_package(SVF REQUIRED CONFIG 
    HINTS ${SVF_DIR} ${SVF_DIR}/Debug-build ${SVF_DIR}/Release-build)
if(SVF_FOUND)
    message(STATUS "Found SVF: ${SVF_VERSION}")
    include_directories(SYSTEM ${SVF_INSTALL_INCLUDE_DIR})
    link_directories(${SVF_INSTALL_LIB_DIR})
else()
    message(FATAL_ERROR "SVF not found")
endif()

# Заголовки Z3 для нашего кода
find_path(Z3_INCLUDES REQUIRED NAMES z3++.h 
    HINTS ${Z3_DIR} ENV Z3_DIR PATH_SUFFIXES include z3)
include_directories(SYSTEM ${Z3_INCLUDES})

# ============================================================
# 4. Сборка нашего проекта
# ============================================================
set(SVF_LIB SvfLLVM SvfCore)
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(svf-mcp-server ${SOURCES})

# РЕШЕНИЕ ПРОБЛЕМЫ 1: Явно указываем, где искать заголовки cpp-mcp
target_include_directories(svf-mcp-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cpp-mcp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cpp-mcp/common
)

# РЕШЕНИЕ ПРОБЛЕМЫ 2: Применяем флаги LLVM ТОЛЬКО к нашему исполняемому файлу
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(svf-mcp-server PRIVATE "-fno-rtti")
endif()
if(NOT LLVM_ENABLE_EH)
    target_compile_options(svf-mcp-server PRIVATE "-fno-exceptions")
endif()

# РЕШЕНИЕ ПРОБЛЕМЫ 3: Разрешаем исключения ТОЛЬКО в файлах, где мы ловим ошибки SVF
set_source_files_properties(
    src/main.cpp
    src/svf_engine/engine.cpp
    src/mcp_tools/tools.cpp
    PROPERTIES COMPILE_OPTIONS "-fexceptions"
)

target_link_libraries(svf-mcp-server PRIVATE
    ${llvm_libs}
    ${SVF_LIB}
    ${Z3_LIBRARIES}
    mcp
)