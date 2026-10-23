// Lua 5.1 - 5.5 compatibility shims. Only what this module needs.
#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#if LUA_VERSION_NUM < 502
#define lua_rawlen(L, i) lua_objlen(L, i)
inline int lua_absindex(lua_State* L, int i) {
  return (i > 0 || i <= LUA_REGISTRYINDEX) ? i : lua_gettop(L) + i + 1;
}
inline void* luaL_testudata(lua_State* L, int ud, const char* tname) {
  void* p = lua_touserdata(L, ud);
  if (p != nullptr && lua_getmetatable(L, ud)) {
    luaL_getmetatable(L, tname);
    if (lua_rawequal(L, -1, -2)) {
      lua_pop(L, 2);
      return p;
    }
    lua_pop(L, 2);
  }
  return nullptr;
}
inline void luaL_setmetatable(lua_State* L, const char* tname) {
  luaL_getmetatable(L, tname);
  lua_setmetatable(L, -2);
}
inline void luaL_setfuncs(lua_State* L, const luaL_Reg* l, int nup) {
  for (; l->name != nullptr; l++) {
    for (int i = 0; i < nup; i++) lua_pushvalue(L, -nup);
    lua_pushcclosure(L, l->func, nup);
    lua_setfield(L, -(nup + 2), l->name);
  }
  lua_pop(L, nup);
}
#endif

#if LUA_VERSION_NUM < 504
#define lua_newuserdatauv(L, sz, nuv) lua_newuserdata(L, sz)
#endif

#if defined(_WIN32)
#define LF_EXPORT extern "C" __declspec(dllexport)
#else
#define LF_EXPORT extern "C" __attribute__((visibility("default")))
#endif
