# Builds a stock Lua interpreter (and, on Windows, the luaXY.dll + import
# library) from an unpacked lua.org source tree. Used by CI to get headers, an
# import library and an interpreter laid out like the LuaBinaries packages:
#
#   Linux:   `lua` executable exporting the Lua API (as LuaBinaries' does), so
#            C modules that are not linked against liblua resolve against it.
#   Windows: luaXY.dll + luaXY.lib + lua.exe importing luaXY.dll by name.
#
#   cmake -S tools/lua -B build-lua -DLUA_SRC=<dir> -DLUA_VERSION=5.4 -G Ninja
cmake_minimum_required(VERSION 3.20)
project(lua-from-source C)

set(LUA_SRC "" CACHE PATH "Unpacked Lua source tree")
set(LUA_VERSION "5.4" CACHE STRING "Lua major.minor (names the DLL)")
string(REPLACE "." "" V "${LUA_VERSION}")

if(EXISTS "${LUA_SRC}/src/lua.h")
  set(SRC "${LUA_SRC}/src")
elseif(EXISTS "${LUA_SRC}/lua.h")
  set(SRC "${LUA_SRC}")
else()
  message(FATAL_ERROR "lua.h not found under LUA_SRC='${LUA_SRC}'")
endif()

file(GLOB CORE "${SRC}/l*.c")
list(FILTER CORE EXCLUDE REGEX "/(lua|luac)\\.c$")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(WIN32)
  add_library(luadll SHARED ${CORE})
  set_target_properties(luadll PROPERTIES OUTPUT_NAME lua${V} PREFIX "")
  target_compile_definitions(luadll PRIVATE LUA_BUILD_AS_DLL _CRT_SECURE_NO_WARNINGS)
  add_executable(lua "${SRC}/lua.c")
  target_compile_definitions(lua PRIVATE LUA_BUILD_AS_DLL _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(lua PRIVATE luadll)
else()
  add_executable(lua "${SRC}/lua.c" ${CORE})
  target_compile_definitions(lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
  target_link_libraries(lua PRIVATE m ${CMAKE_DL_LIBS})
  target_link_options(lua PRIVATE -Wl,-E)  # export the Lua API to C modules
endif()
