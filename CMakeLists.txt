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

# All programs land in <build>/bin (also with multi-config generators such as Visual Studio).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${cfg} CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/formats/*.cpp src/disasm/*.cpp src/analysis/*.cpp src/decompiler/*.cpp
  src/project/*.cpp src/session/*.cpp src/report/*.cpp)
add_library(reversa_core STATIC ${CORE_SOURCES})
target_include_directories(reversa_core PUBLIC src)
target_link_libraries(reversa_core PUBLIC capstone SQLite::SQLite3)
target_compile_definitions(reversa_core PUBLIC REVERSA_VERSION="${PROJECT_VERSION}")

if(REVERSA_PYTHON)
  find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)
  FetchContent_Declare(pybind11 GIT_REPOSITORY https://github.com/pybind/pybind11.git GIT_TAG v2.13.6 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(pybind11)
  # Bindings shared by the `reversa` Python extension and the embedded interpreter of the app.
  add_library(reversa_bindings STATIC src/python/bindings.cpp)
  target_link_libraries(reversa_bindings PUBLIC reversa_core pybind11::pybind11)
  pybind11_add_module(_reversa src/python/module.cpp)
  target_link_libraries(_reversa PRIVATE reversa_bindings)
  # Importable package in the build tree: <build>/bin/python/reversa/{*.py,_reversa.*}
  set(RV_PY_OUT ${CMAKE_BINARY_DIR}/bin/python/reversa)
  set_target_properties(_reversa PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${RV_PY_OUT} LIBRARY_OUTPUT_DIRECTORY_RELEASE ${RV_PY_OUT}
                        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${RV_PY_OUT} RUNTIME_OUTPUT_DIRECTORY ${RV_PY_OUT})
  file(GLOB RV_PY_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/python/reversa/*.py)
  add_custom_target(reversa_python ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RV_PY_OUT}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RV_PY_SOURCES} ${RV_PY_OUT}
    DEPENDS ${RV_PY_SOURCES} COMMENT "Copying the reversa Python package")
  add_dependencies(_reversa reversa_python)
endif()

include(cmake/embed.cmake)
rv_embed(rv_demo_elf ${CMAKE_SOURCE_DIR}/assets/demo/login_x64.elf ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)

file(GLOB CLI_SOURCES CONFIGURE_DEPENDS src/cli/*.cpp)
if(CLI_SOURCES)
  add_executable(reversa ${CLI_SOURCES} ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)
  target_link_libraries(reversa PRIVATE reversa_core)
  if(REVERSA_PYTHON)
    target_sources(reversa PRIVATE src/python/embed.cpp)
    target_link_libraries(reversa PRIVATE reversa_bindings pybind11::embed)
    target_compile_definitions(reversa PRIVATE REVERSA_WITH_PYTHON=1)
    add_dependencies(reversa reversa_python)
    if(WIN32)
      # Python is optional at runtime: delay-load its DLL so REVERSA starts without it.
      set(RV_PY_DLL "python${Python_VERSION_MAJOR}${Python_VERSION_MINOR}.dll")
      target_compile_definitions(reversa PRIVATE REVERSA_PYTHON_DLL="${RV_PY_DLL}")
      target_link_options(reversa PRIVATE "/DELAYLOAD:${RV_PY_DLL}")
      target_link_libraries(reversa PRIVATE delayimp)
    endif()
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
