cmake_minimum_required(VERSION 3.21)
project(reversa VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(REVERSA_GUI "Build the Qt GUI" ON)
option(REVERSA_PYTHON "Build the Python scripting module and embedded interpreter" ON)
option(REVERSA_TESTS "Build the C++ tests" ON)

if(MSVC)
  add_compile_options(/utf-8 /W3 /EHsc /bigobj)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include(FetchContent)
# Capstone (BSD-3-Clause): disassembler engine, built statically with only the architectures we use.
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
foreach(a X86 ARM ARM64)
  set(CAPSTONE_${a}_SUPPORT ON CACHE BOOL "" FORCE)
endforeach()
set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(capstone GIT_REPOSITORY https://github.com/capstone-engine/capstone.git GIT_TAG 5.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(capstone)

find_package(SQLite3 REQUIRED)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/formats/*.cpp src/disasm/*.cpp src/analysis/*.cpp src/decompiler/*.cpp
  src/project/*.cpp src/session/*.cpp src/report/*.cpp)
add_library(reversa_core STATIC ${CORE_SOURCES})
target_include_directories(reversa_core PUBLIC src)
target_link_libraries(reversa_core PUBLIC capstone SQLite::SQLite3)
target_compile_definitions(reversa_core PUBLIC REVERSA_VERSION="${PROJECT_VERSION}")

if(REVERSA_PYTHON)
  find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)
  find_package(pybind11 CONFIG REQUIRED)
  # Bindings shared by the `reversa` Python extension and the embedded interpreter of the app.
  add_library(reversa_bindings STATIC src/python/bindings.cpp)
  target_link_libraries(reversa_bindings PUBLIC reversa_core pybind11::pybind11)
  pybind11_add_module(_reversa src/python/module.cpp)
  target_link_libraries(_reversa PRIVATE reversa_bindings)
endif()

include(cmake/embed.cmake)
rv_embed(rv_demo_elf ${CMAKE_SOURCE_DIR}/assets/demo/login_x64.elf ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)

file(GLOB CLI_SOURCES CONFIGURE_DEPENDS src/cli/*.cpp)
if(CLI_SOURCES)
  add_executable(reversa ${CLI_SOURCES} ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)
  target_link_libraries(reversa PRIVATE reversa_core)
  if(REVERSA_PYTHON)
    target_link_libraries(reversa PRIVATE reversa_bindings pybind11::embed)
    target_compile_definitions(reversa PRIVATE REVERSA_WITH_PYTHON=1)
  endif()
endif()

if(REVERSA_TESTS)
  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/cpp/*.cpp)
  add_executable(reversa_tests ${TEST_SOURCES})
  target_link_libraries(reversa_tests PRIVATE reversa_core)
  target_compile_definitions(reversa_tests PRIVATE REVERSA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME reversa_tests COMMAND reversa_tests)
endif()

if(REVERSA_TESTS)
  add_executable(rvdump tools/rvdump.cpp)
  target_link_libraries(rvdump PRIVATE reversa_core)
endif()
