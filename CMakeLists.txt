cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)  # honour CMAKE_MSVC_RUNTIME_LIBRARY
project(lua-ftxui VERSION 0.1.0 LANGUAGES CXX)

set(LUA_VERSION "5.4" CACHE STRING "Lua version to build for (5.1 .. 5.5); only used to locate headers")
set(LUA_INCLUDE_DIR "" CACHE PATH "Directory containing lua.h / lauxlib.h")
set(LUA_LIBRARY "" CACHE FILEPATH "Windows: import library of luaXY.dll (e.g. lua54.lib). Unused elsewhere.")
set(FTXUI_TAG "v6.1.9" CACHE STRING "FTXUI release to build against")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
# Static C/C++ runtime on Windows: the DLL must not need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- Lua headers -------------------------------------------------------------
string(REPLACE "." "" LUA_VERSION_NODOT "${LUA_VERSION}")
if(NOT LUA_INCLUDE_DIR)
  find_path(LUA_INCLUDE_DIR lua.h
    PATH_SUFFIXES lua${LUA_VERSION} lua${LUA_VERSION_NODOT} lua-${LUA_VERSION} lua)
endif()
if(NOT LUA_INCLUDE_DIR OR NOT EXISTS "${LUA_INCLUDE_DIR}/lua.h")
  message(FATAL_ERROR "lua.h not found: pass -DLUA_INCLUDE_DIR=<dir>")
endif()
if(WIN32 AND NOT LUA_LIBRARY)
  message(FATAL_ERROR "On Windows pass -DLUA_LIBRARY=<path to luaXY.lib> (import library of the Lua DLL)")
endif()

# ---- FTXUI ---------------------------------------------------------------------
include(FetchContent)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG ${FTXUI_TAG}
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ftxui)
string(REGEX REPLACE "^v" "" FTXUI_VERSION_STRING "${FTXUI_TAG}")

# ---- the module ----------------------------------------------------------------
add_library(lua_ftxui MODULE
  src/core.cpp
  src/dom.cpp
  src/component.cpp
  src/screen.cpp
  src/module.cpp)
set_target_properties(lua_ftxui PROPERTIES OUTPUT_NAME ftxui PREFIX "")
target_include_directories(lua_ftxui PRIVATE "${LUA_INCLUDE_DIR}")
target_compile_definitions(lua_ftxui PRIVATE
  LUA_FTXUI_VERSION="${PROJECT_VERSION}"
  LUA_FTXUI_FTXUI_VERSION="${FTXUI_VERSION_STRING}")
target_link_libraries(lua_ftxui PRIVATE ftxui::component ftxui::dom ftxui::screen)

if(WIN32)
  target_compile_definitions(lua_ftxui PRIVATE LUA_BUILD_AS_DLL NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(lua_ftxui PRIVATE "${LUA_LIBRARY}")
  if(MSVC)
    target_compile_options(lua_ftxui PRIVATE /utf-8 /W3)
  elseif(MINGW)
    target_link_options(lua_ftxui PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
else()
  # Lua symbols are resolved from the host interpreter at load time.
  target_compile_options(lua_ftxui PRIVATE -Wall -Wextra)
  target_link_options(lua_ftxui PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports.map")
endif()

install(TARGETS lua_ftxui
  LIBRARY DESTINATION lib/lua/${LUA_VERSION}
  RUNTIME DESTINATION lib/lua/${LUA_VERSION})
