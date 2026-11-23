cmake_minimum_required(VERSION 3.21)
project(reversa VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(REVERSA_GUI "Build the Qt GUI" ON)
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
  src/project/*.cpp src/session/*.cpp src/report/*.cpp src/plugins/*.cpp)
add_library(reversa_core STATIC ${CORE_SOURCES})
target_include_directories(reversa_core PUBLIC src sdk/include)
target_link_libraries(reversa_core PUBLIC capstone SQLite::SQLite3 ${CMAKE_DL_LIBS})
target_compile_definitions(reversa_core PUBLIC REVERSA_VERSION="${PROJECT_VERSION}")

include(cmake/embed.cmake)
rv_embed(rv_demo_elf ${CMAKE_SOURCE_DIR}/assets/demo/login_x64.elf ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)

# Application layer shared by the executables and the GUI tests: CLI commands, demo, Qt GUI.
add_library(reversa_app STATIC src/cli/main.cpp ${CMAKE_BINARY_DIR}/generated/demo_elf.cpp)
target_link_libraries(reversa_app PUBLIC reversa_core)
if(REVERSA_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
  file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp src/gui/*.hpp)
  list(FILTER GUI_SOURCES EXCLUDE REGEX "entry_windows\\.cpp$")
  target_sources(reversa_app PRIVATE ${GUI_SOURCES})
  set_target_properties(reversa_app PROPERTIES AUTOMOC ON)
  target_link_libraries(reversa_app PUBLIC Qt6::Widgets)
  target_compile_definitions(reversa_app PUBLIC REVERSA_WITH_GUI=1 QT_NO_KEYWORDS)
  file(GLOB GUI_TRANSLATIONS CONFIGURE_DEPENDS src/gui/i18n/*.json)
  qt_add_resources(reversa_app rv_i18n PREFIX "/" BASE src/gui FILES ${GUI_TRANSLATIONS})
  qt_add_resources(reversa_app rv_icons PREFIX "/icons" BASE assets FILES assets/reversa-32.png assets/reversa-256.png)
endif()


if(WIN32)
  # .exe icon in Explorer and the taskbar
  set(RV_WIN_ICON ${CMAKE_BINARY_DIR}/generated/reversa.rc)
  file(WRITE ${RV_WIN_ICON} "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/assets/reversa.ico\"\n")
endif()
add_executable(reversa src/cli/entry.cpp ${RV_WIN_ICON})
target_link_libraries(reversa PRIVATE reversa_app)
if(WIN32 AND REVERSA_GUI)
  add_executable(reversa-gui WIN32 src/gui/entry_windows.cpp ${RV_WIN_ICON})
  target_link_libraries(reversa-gui PRIVATE reversa_app)
endif()

# ---------------------------------------------------------------- examples: plugins and scripts (shared libraries)
# Built against the SDK headers only, exactly like third-party plugins; output: <build>/bin/examples/<kind>/<name>.so|.dll
function(rv_module name src out)
  add_library(${name} MODULE ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/sdk/include)
  set_target_properties(${name} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
                        CXX_STANDARD 17 LIBRARY_OUTPUT_DIRECTORY ${out})
  foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${CFG} ${out})
  endforeach()
endfunction()
foreach(ex plugins/hello_plugin plugins/crypto_constants plugins/crashing_plugin
           scripts/strcmp_callers scripts/function_table scripts/annotate_and_view)
  get_filename_component(kind ${ex} DIRECTORY)
  get_filename_component(name ${ex} NAME)
  rv_module(${name} examples/${ex}.cpp ${CMAKE_BINARY_DIR}/bin/examples/${kind})
  list(APPEND RV_EXAMPLES_${kind} ${name})
endforeach()

# ---------------------------------------------------------------- install / packages
# Windows: everything next to the executables (portable zip). Linux: FHS layout for the .deb.
install(TARGETS reversa RUNTIME DESTINATION bin)
if(TARGET reversa-gui)
  install(TARGETS reversa-gui RUNTIME DESTINATION bin)
endif()
if(WIN32)
  set(RV_SHARE bin)
  set(RV_DOC bin/doc)
  set(RV_SDK_INCLUDE bin/sdk/include)
  set(RV_LIB_EXAMPLES bin/examples)
  include(InstallRequiredSystemLibraries)  # MSVC runtime DLLs
else()
  set(RV_SHARE share/reversa)
  set(RV_DOC share/doc/reversa)
  set(RV_SDK_INCLUDE include)
  set(RV_LIB_EXAMPLES lib/reversa/examples)
  install(FILES assets/reversa.desktop DESTINATION share/applications)
  install(FILES assets/reversa.svg DESTINATION share/icons/hicolor/scalable/apps)
  install(FILES assets/reversa-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME reversa.png)
endif()
install(DIRECTORY examples/ DESTINATION ${RV_SHARE}/examples)           # example sources
install(DIRECTORY sdk/include/reversa DESTINATION ${RV_SDK_INCLUDE})    # headers for plugins and scripts
install(TARGETS ${RV_EXAMPLES_plugins} LIBRARY DESTINATION ${RV_LIB_EXAMPLES}/plugins)  # built examples
install(TARGETS ${RV_EXAMPLES_scripts} LIBRARY DESTINATION ${RV_LIB_EXAMPLES}/scripts)
install(FILES README.md LICENSE LICENSE-THIRD-PARTY DESTINATION ${RV_DOC})
install(DIRECTORY docs/ DESTINATION ${RV_DOC}/docs)

set(CPACK_PACKAGE_NAME reversa)
set(CPACK_PACKAGE_VENDOR "REVERSA contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source reverse engineering tool: disassembler, pseudocode, x-refs, graphs, C/C++ plugins")
set(CPACK_PACKAGE_CONTACT "REVERSA contributors <https://github.com/MrKniha/reversa>")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_DEBIAN_PACKAGE_SECTION devel)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # Qt and SQLite versions from the build machine
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/MrKniha/reversa")
if(WIN32)
  # Portable zip: `cpack -G ZIP` -> reversa-windows-x64.zip with a reversa-windows-x64/ folder inside.
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_FILE_NAME reversa-windows-x64)
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
  # Installer: `cpack -G NSIS` -> Program Files, Start menu entry (found by Windows search),
  # optional desktop shortcut and PATH entry, uninstaller in Settings > Apps.
  set(CPACK_PACKAGE_INSTALL_DIRECTORY REVERSA)
  set(CPACK_PACKAGE_EXECUTABLES reversa-gui REVERSA)
  set(CPACK_CREATE_DESKTOP_LINKS reversa-gui)
  set(CPACK_NSIS_PACKAGE_NAME REVERSA)
  set(CPACK_NSIS_DISPLAY_NAME REVERSA)
  set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
  set(CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/assets/reversa.ico)
  set(CPACK_NSIS_MUI_UNIICON ${CMAKE_SOURCE_DIR}/assets/reversa.ico)
  set(CPACK_NSIS_INSTALLED_ICON_NAME bin\\\\reversa-gui.exe)
  set(CPACK_NSIS_MODIFY_PATH ON)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/MrKniha/reversa")
  if(TARGET reversa-gui)
    # Qt DLLs, plugins and Qt's own translations next to the executables (dynamic linking, LGPL).
    find_program(WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" REQUIRED)
    install(CODE "execute_process(COMMAND \"${WINDEPLOYQT}\" --release --no-system-d3d-compiler --no-opengl-sw
                  \"\${CMAKE_INSTALL_PREFIX}/bin/reversa-gui.exe\" COMMAND_ERROR_IS_FATAL ANY)")
  endif()
endif()
include(CPack)

if(REVERSA_TESTS)
  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/cpp/*.cpp)
  add_executable(reversa_tests ${TEST_SOURCES})
  target_link_libraries(reversa_tests PRIVATE reversa_core)
  target_compile_definitions(reversa_tests PRIVATE REVERSA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                             REVERSA_EXAMPLES_DIR="${CMAKE_BINARY_DIR}/bin/examples" REVERSA_EXE="$<TARGET_FILE:reversa>")
  # test-only C modules (bad plugins, a C plugin, a script with an exit code): <build>/bin/test-modules/<kind>
  foreach(m plugins/counter plugins/old_api plugins/null_entry plugins/no_entry plugins/failing_load scripts/exit_code)
    get_filename_component(kind ${m} DIRECTORY)
    get_filename_component(name ${m} NAME)
    rv_module(test_${name} tests/cpp/plugins/${name}.c ${CMAKE_BINARY_DIR}/bin/test-modules/${kind})
    set_target_properties(test_${name} PROPERTIES OUTPUT_NAME ${name})
    list(APPEND RV_TEST_MODULES test_${name})
  endforeach()
  target_compile_definitions(reversa_tests PRIVATE REVERSA_TEST_MODULES_DIR="${CMAKE_BINARY_DIR}/bin/test-modules")
  add_dependencies(reversa_tests reversa ${RV_EXAMPLES_plugins} ${RV_EXAMPLES_scripts} ${RV_TEST_MODULES})
  add_test(NAME reversa_tests COMMAND reversa_tests)
  if(REVERSA_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(reversa_gui_tests tests/gui/test_gui.cpp)
    set_target_properties(reversa_gui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(reversa_gui_tests PRIVATE reversa_app Qt6::Test)
    target_compile_definitions(reversa_gui_tests PRIVATE REVERSA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                               REVERSA_EXAMPLES_DIR="${CMAKE_BINARY_DIR}/bin/examples")
    add_dependencies(reversa_gui_tests ${RV_EXAMPLES_plugins})
    add_test(NAME reversa_gui_tests COMMAND reversa_gui_tests)
    set_tests_properties(reversa_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()

if(REVERSA_TESTS)
  add_executable(rvdump tools/rvdump.cpp)
  target_link_libraries(rvdump PRIVATE reversa_core)
  # repository checks used by CI (standard C++ only, they also build without CMake: c++ -std=c++20 tools/X.cpp)
  foreach(tool check_licenses i18n_check headless_check)
    add_executable(${tool} tools/${tool}.cpp)
  endforeach()
endif()
