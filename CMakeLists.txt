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
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
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

# Application layer shared by the executables and the GUI tests: CLI commands, demo, embedded
# Python, Qt GUI.
add_library(reversa_app STATIC src/cli/main.cpp ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)
target_link_libraries(reversa_app PUBLIC reversa_core)
if(REVERSA_PYTHON)
  target_sources(reversa_app PRIVATE src/python/embed.cpp)
  target_link_libraries(reversa_app PUBLIC reversa_bindings pybind11::embed)
  target_compile_definitions(reversa_app PUBLIC REVERSA_WITH_PYTHON=1)
  add_dependencies(reversa_app reversa_python)
endif()
if(REVERSA_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
  file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp src/gui/*.hpp)
  list(FILTER GUI_SOURCES EXCLUDE REGEX "entry_windows\\.cpp$")
  target_sources(reversa_app PRIVATE ${GUI_SOURCES})
  set_target_properties(reversa_app PROPERTIES AUTOMOC ON)
  target_link_libraries(reversa_app PUBLIC Qt6::Widgets)
  target_compile_definitions(reversa_app PUBLIC REVERSA_WITH_GUI=1 QT_NO_KEYWORDS)
endif()

function(rv_python_delayload target)
  if(WIN32 AND REVERSA_PYTHON)
    # Python is optional at runtime: delay-load its DLL so REVERSA starts without it.
    set(dll "python${Python_VERSION_MAJOR}${Python_VERSION_MINOR}.dll")
    target_compile_definitions(${target} PRIVATE REVERSA_PYTHON_DLL="${dll}")
    target_link_options(${target} PRIVATE "/DELAYLOAD:${dll}")
    target_link_libraries(${target} PRIVATE delayimp)
  endif()
endfunction()

add_executable(reversa src/cli/entry.cpp)
target_link_libraries(reversa PRIVATE reversa_app)
rv_python_delayload(reversa)
if(WIN32 AND REVERSA_GUI)
  add_executable(reversa-gui WIN32 src/gui/entry_windows.cpp)
  target_link_libraries(reversa-gui PRIVATE reversa_app)
  rv_python_delayload(reversa-gui)
endif()

# ---------------------------------------------------------------- install / packages
# Windows: everything next to the executables (portable zip). Linux: FHS layout for the .deb.
install(TARGETS reversa RUNTIME DESTINATION bin)
if(TARGET reversa-gui)
  install(TARGETS reversa-gui RUNTIME DESTINATION bin)
endif()
if(WIN32)
  set(RV_SHARE bin)
  set(RV_DOC bin/doc)
  include(InstallRequiredSystemLibraries)  # MSVC runtime DLLs
else()
  set(RV_SHARE share/reversa)
  set(RV_DOC share/doc/reversa)
  install(FILES assets/reversa.desktop DESTINATION share/applications)
  install(FILES assets/reversa.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()
if(REVERSA_PYTHON)
  install(FILES ${RV_PY_SOURCES} DESTINATION ${RV_SHARE}/python/reversa)
  install(TARGETS _reversa LIBRARY DESTINATION ${RV_SHARE}/python/reversa RUNTIME DESTINATION ${RV_SHARE}/python/reversa)
endif()
install(DIRECTORY examples/ DESTINATION ${RV_SHARE}/examples PATTERN "__pycache__" EXCLUDE)
install(FILES README.md LICENSE LICENSE-THIRD-PARTY DESTINATION ${RV_DOC})
install(DIRECTORY docs/ DESTINATION ${RV_DOC}/docs)

set(CPACK_PACKAGE_NAME reversa)
set(CPACK_PACKAGE_VENDOR "REVERSA contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source reverse engineering tool: disassembler, pseudocode, x-refs, graphs, Python API")
set(CPACK_PACKAGE_CONTACT "REVERSA contributors <https://github.com/MrKniha/reversa>")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_DEBIAN_PACKAGE_SECTION devel)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # Qt, SQLite and libpython versions from the build machine
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/MrKniha/reversa")
include(CPack)

if(REVERSA_TESTS)
  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/cpp/*.cpp)
  add_executable(reversa_tests ${TEST_SOURCES})
  target_link_libraries(reversa_tests PRIVATE reversa_core)
  target_compile_definitions(reversa_tests PRIVATE REVERSA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME reversa_tests COMMAND reversa_tests)
  if(REVERSA_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(reversa_gui_tests tests/gui/test_gui.cpp)
    set_target_properties(reversa_gui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(reversa_gui_tests PRIVATE reversa_app Qt6::Test)
    target_compile_definitions(reversa_gui_tests PRIVATE REVERSA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    rv_python_delayload(reversa_gui_tests)
    add_test(NAME reversa_gui_tests COMMAND reversa_gui_tests)
    set_tests_properties(reversa_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()

if(REVERSA_TESTS)
  add_executable(rvdump tools/rvdump.cpp)
  target_link_libraries(rvdump PRIVATE reversa_core)
endif()
