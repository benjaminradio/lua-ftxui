#include "core.hpp"

#ifndef LUA_FTXUI_VERSION
#define LUA_FTXUI_VERSION "0.0.0"
#endif
#ifndef LUA_FTXUI_FTXUI_VERSION
#define LUA_FTXUI_FTXUI_VERSION "unknown"
#endif

LF_EXPORT int luaopen_ftxui(lua_State* L) {
  lf::set_state(L);
  lf::init_metatables(L);
  lua_newtable(L);
  lf::register_dom(L, -1);
  lf::register_components(L, -1);
  lf::register_screen(L, -1);
  lua_pushstring(L, LUA_FTXUI_VERSION);
  lua_setfield(L, -2, "version");
  lua_pushstring(L, LUA_FTXUI_FTXUI_VERSION);
  lua_setfield(L, -2, "ftxui_version");
  return 1;
}
