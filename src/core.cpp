#include "core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <new>

#include <ftxui/component/screen_interactive.hpp>

namespace lf {

lua_State* g_L = nullptr;
std::string g_pending;
static std::vector<int> g_dead_refs;

void set_state(lua_State* L) {
  g_L = L;
  if (!g_dead_refs.empty()) {
    std::vector<int> dead;
    dead.swap(g_dead_refs);
    for (int r : dead) luaL_unref(L, LUA_REGISTRYINDEX, r);
  }
}

void record_error(const std::string& msg) {
  if (g_pending.empty()) g_pending = msg.empty() ? "unknown error" : msg;
  if (auto* s = ftxui::ScreenInteractive::Active()) s->Exit();
}

int guard_call(lua_State* L, int (*f)(lua_State*)) {
  int n = 0;
  std::string err;
  bool from_callback = false;
  try {
    set_state(L);
    n = f(L);
  } catch (const std::exception& e) {
    err = e.what();
  } catch (...) {
    err = "unknown C++ exception";
  }
  if (err.empty() && !g_pending.empty()) {
    err.swap(g_pending);
    g_pending.clear();
    from_callback = true;
  }
  if (err.empty()) return n;
  if (from_callback) {
    lua_pushlstring(L, err.data(), err.size());
  } else {
    luaL_where(L, 1);
    lua_pushlstring(L, err.data(), err.size());
    lua_concat(L, 2);
  }
  return -1;  // `err` is destroyed on return, before the caller raises
}

// ---- Lua refs --------------------------------------------------------------

LuaRef::LuaRef(lua_State* L, int idx) {
  lua_pushvalue(L, idx);
  ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
}
LuaRef::~LuaRef() { g_dead_refs.push_back(ref_); }
void LuaRef::push(lua_State* L) const { lua_rawgeti(L, LUA_REGISTRYINDEX, ref_); }

Fn check_fn(lua_State* L, int idx) {
  if (lua_type(L, idx) != LUA_TFUNCTION) arg_error(idx, "function expected");
  return std::make_shared<LuaRef>(L, idx);
}

Fn opt_fn_field(lua_State* L, int tbl, const char* k) {
  lua_getfield(L, tbl, k);
  Fn f;
  int t = lua_type(L, -1);
  if (t == LUA_TFUNCTION) {
    f = std::make_shared<LuaRef>(L, -1);
  } else if (t != LUA_TNIL) {
    lua_pop(L, 1);
    throw LuaError(std::string("field '") + k + "' must be a function");
  }
  lua_pop(L, 1);
  return f;
}

static int traceback_handler(lua_State* L) {
  const char* msg = lua_tostring(L, 1);
  if (msg == nullptr) {
    if (!lua_isnoneornil(L, 1) && luaL_callmeta(L, 1, "__tostring") &&
        lua_type(L, -1) == LUA_TSTRING)
      return 1;
    msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
  }
#if LUA_VERSION_NUM >= 502
  luaL_traceback(L, L, msg, 1);
#else
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "traceback");
    if (lua_isfunction(L, -1)) {
      lua_pushstring(L, msg);
      lua_pushinteger(L, 2);
      lua_call(L, 2, 1);
      return 1;
    }
  }
  lua_pushstring(L, msg);
#endif
  return 1;
}

bool call_fn(const Fn& f, const std::function<int(lua_State*)>& push, int nres,
             const std::function<void(lua_State*)>& take) {
  lua_State* L = g_L;
  if (L == nullptr || !f) return false;
  int top = lua_gettop(L);
  lua_checkstack(L, 12);
  lua_pushcfunction(L, traceback_handler);
  int handler = lua_gettop(L);
  f->push(L);
  int n = push ? push(L) : 0;
  if (lua_pcall(L, n, nres, handler) != 0) {
    const char* m = lua_tostring(L, -1);
    std::string msg = m ? m : "(error object)";
    lua_settop(L, top);
    record_error(msg);
    return false;
  }
  if (take && nres > 0) take(L);
  lua_settop(L, top);
  return true;
}

void call_void(const Fn& f) { call_fn(f, nullptr, 0, nullptr); }
void call_void(const Fn& f, const std::function<int(lua_State*)>& push) {
  call_fn(f, push, 0, nullptr);
}
bool call_bool(const Fn& f, const std::function<int(lua_State*)>& push) {
  bool r = false;
  call_fn(f, push, 1, [&r](lua_State* L) { r = lua_toboolean(L, -1) != 0; });
  return r;
}

// ---- arguments -----------------------------------------------------------------

void arg_error(int idx, const std::string& what) {
  throw LuaError("bad argument #" + std::to_string(idx) + " (" + what + ")");
}

std::string check_string(lua_State* L, int idx) {
  int t = lua_type(L, idx);
  if (t != LUA_TSTRING && t != LUA_TNUMBER) arg_error(idx, "string expected");
  size_t len = 0;
  const char* s = lua_tolstring(L, idx, &len);
  return std::string(s, len);
}

std::string opt_string(lua_State* L, int idx, const std::string& def) {
  if (lua_isnoneornil(L, idx)) return def;
  return check_string(L, idx);
}

double check_number(lua_State* L, int idx) {
  if (lua_type(L, idx) != LUA_TNUMBER &&
      !(lua_type(L, idx) == LUA_TSTRING && lua_isnumber(L, idx)))
    arg_error(idx, "number expected");
  return lua_tonumber(L, idx);
}

int check_int(lua_State* L, int idx) { return static_cast<int>(check_number(L, idx)); }

bool is_plain_table(lua_State* L, int idx) { return lua_type(L, idx) == LUA_TTABLE; }

bool field_present(lua_State* L, int tbl, const char* k) {
  lua_getfield(L, tbl, k);
  bool r = !lua_isnil(L, -1);
  lua_pop(L, 1);
  return r;
}

bool field_string(lua_State* L, int tbl, const char* k, std::string& out) {
  lua_getfield(L, tbl, k);
  int t = lua_type(L, -1);
  bool ok = false;
  if (t == LUA_TSTRING || t == LUA_TNUMBER) {
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    out.assign(s, len);
    ok = true;
  } else if (t != LUA_TNIL) {
    lua_pop(L, 1);
    throw LuaError(std::string("field '") + k + "' must be a string");
  }
  lua_pop(L, 1);
  return ok;
}

bool field_number(lua_State* L, int tbl, const char* k, double& out) {
  lua_getfield(L, tbl, k);
  int t = lua_type(L, -1);
  bool ok = false;
  if (t == LUA_TNUMBER || (t == LUA_TSTRING && lua_isnumber(L, -1))) {
    out = lua_tonumber(L, -1);
    ok = true;
  } else if (t != LUA_TNIL) {
    lua_pop(L, 1);
    throw LuaError(std::string("field '") + k + "' must be a number");
  }
  lua_pop(L, 1);
  return ok;
}

bool field_int(lua_State* L, int tbl, const char* k, int& out) {
  double d;
  if (!field_number(L, tbl, k, d)) return false;
  out = static_cast<int>(d);
  return true;
}

bool field_bool(lua_State* L, int tbl, const char* k, bool& out) {
  lua_getfield(L, tbl, k);
  bool present = !lua_isnil(L, -1);
  if (present) out = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return present;
}

// ---- colors ---------------------------------------------------------------------

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static ftxui::Color parse_color(const std::string& raw) {
  using ftxui::Color;
  std::string s = lower(raw);
  if (!s.empty() && s[0] == '#') {
    std::vector<int> d;
    for (size_t i = 1; i < s.size(); i++) {
      int v = hexval(s[i]);
      if (v < 0) throw LuaError("invalid color '" + raw + "'");
      d.push_back(v);
    }
    if (d.size() == 3) return Color::RGB(d[0] * 17, d[1] * 17, d[2] * 17);
    if (d.size() == 6)
      return Color::RGB(d[0] * 16 + d[1], d[2] * 16 + d[3], d[4] * 16 + d[5]);
    throw LuaError("invalid color '" + raw + "' (use #rgb or #rrggbb)");
  }
  static const std::map<std::string, Color> names = {
      {"default", Color()},
      {"black", Color(Color::Black)},
      {"red", Color(Color::Red)},
      {"green", Color(Color::Green)},
      {"yellow", Color(Color::Yellow)},
      {"blue", Color(Color::Blue)},
      {"magenta", Color(Color::Magenta)},
      {"cyan", Color(Color::Cyan)},
      {"gray_light", Color(Color::GrayLight)},
      {"light_gray", Color(Color::GrayLight)},
      {"silver", Color(Color::GrayLight)},
      {"gray", Color(Color::GrayDark)},
      {"grey", Color(Color::GrayDark)},
      {"gray_dark", Color(Color::GrayDark)},
      {"dark_gray", Color(Color::GrayDark)},
      {"red_light", Color(Color::RedLight)},
      {"bright_red", Color(Color::RedLight)},
      {"green_light", Color(Color::GreenLight)},
      {"bright_green", Color(Color::GreenLight)},
      {"yellow_light", Color(Color::YellowLight)},
      {"bright_yellow", Color(Color::YellowLight)},
      {"blue_light", Color(Color::BlueLight)},
      {"bright_blue", Color(Color::BlueLight)},
      {"magenta_light", Color(Color::MagentaLight)},
      {"bright_magenta", Color(Color::MagentaLight)},
      {"cyan_light", Color(Color::CyanLight)},
      {"bright_cyan", Color(Color::CyanLight)},
      {"white", Color(Color::White)},
  };
  auto it = names.find(s);
  if (it == names.end()) throw LuaError("unknown color '" + raw + "'");
  return it->second;
}

ftxui::Color to_color(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  int t = lua_type(L, idx);
  if (t == LUA_TSTRING) return parse_color(check_string(L, idx));
  if (t == LUA_TNUMBER) {
    int n = static_cast<int>(lua_tonumber(L, idx));
    if (n < 0 || n > 255) throw LuaError("palette color must be 0..255");
    return ftxui::Color(static_cast<ftxui::Color::Palette256>(n));
  }
  if (t == LUA_TTABLE) {
    int c[3] = {0, 0, 0};
    const char* names[3] = {"r", "g", "b"};
    for (int i = 0; i < 3; i++) {
      lua_rawgeti(L, idx, i + 1);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getfield(L, idx, names[i]);
      }
      if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        throw LuaError("color table must be {r, g, b}");
      }
      c[i] = std::max(0, std::min(255, static_cast<int>(lua_tonumber(L, -1))));
      lua_pop(L, 1);
    }
    return ftxui::Color::RGB(c[0], c[1], c[2]);
  }
  throw LuaError("color must be a name, '#rrggbb', palette index or {r,g,b}");
}

// ---- userdata -------------------------------------------------------------------

static int elem_gc(lua_State* L) {
  auto* b = static_cast<ElemBox*>(luaL_testudata(L, 1, kElementMT));
  if (b) b->~ElemBox();
  return 0;
}

static int comp_gc(lua_State* L) {
  auto* b = static_cast<CompBox*>(luaL_testudata(L, 1, kComponentMT));
  if (b) b->~CompBox();
  return 0;
}

void init_metatables(lua_State* L) {
  luaL_newmetatable(L, kElementMT);
  lua_pushcfunction(L, elem_gc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);
  luaL_newmetatable(L, kComponentMT);
  lua_pushcfunction(L, comp_gc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);
}

void push_element(lua_State* L, ftxui::Element e) {
  void* mem = lua_newuserdatauv(L, sizeof(ElemBox), 0);
  new (mem) ElemBox{std::move(e)};
  luaL_setmetatable(L, kElementMT);
}

void push_component(lua_State* L, ftxui::Component c, std::shared_ptr<Model> m) {
  void* mem = lua_newuserdatauv(L, sizeof(CompBox), 0);
  new (mem) CompBox{std::move(c), std::move(m)};
  luaL_setmetatable(L, kComponentMT);
}

CompBox* test_component(lua_State* L, int idx) {
  return static_cast<CompBox*>(luaL_testudata(L, idx, kComponentMT));
}

ftxui::Component check_component(lua_State* L, int idx) {
  CompBox* b = test_component(L, idx);
  if (!b) arg_error(idx, "component expected");
  return b->c;
}

ftxui::Element to_element(lua_State* L, int idx) {
  if (auto* b = static_cast<ElemBox*>(luaL_testudata(L, idx, kElementMT))) return b->e;
  if (auto* c = test_component(L, idx)) return c->c->Render();
  int t = lua_type(L, idx);
  if (t == LUA_TSTRING || t == LUA_TNUMBER) return ftxui::text(check_string(L, idx));
  arg_error(idx, "element, component or string expected");
}

ftxui::Elements to_elements(lua_State* L, int tbl) {
  tbl = lua_absindex(L, tbl);
  ftxui::Elements out;
  size_t n = lua_rawlen(L, tbl);
  for (size_t i = 1; i <= n; i++) {
    lua_rawgeti(L, tbl, static_cast<int>(i));
    out.push_back(to_element(L, -1));
    lua_pop(L, 1);
  }
  return out;
}

// Aliasing shared_ptr: the returned handle points at `inner` but owns a holder
// that also owns the model (and, through `inner`, every callback).
ftxui::Component make_component(ftxui::Component inner, std::shared_ptr<Model> m) {
  struct Holder {
    ftxui::Component inner;
    std::shared_ptr<Model> model;
  };
  auto h = std::make_shared<Holder>(Holder{std::move(inner), std::move(m)});
  ftxui::ComponentBase* raw = h->inner.get();
  return ftxui::Component(h, raw);
}

// ---- events ---------------------------------------------------------------------

namespace {

using ftxui::Event;

const std::vector<std::pair<std::string, Event>>& key_table() {
  static const std::vector<std::pair<std::string, Event>> t = [] {
    std::vector<std::pair<std::string, Event>> v = {
        {"ArrowLeft", Event::ArrowLeft},       {"ArrowRight", Event::ArrowRight},
        {"ArrowUp", Event::ArrowUp},           {"ArrowDown", Event::ArrowDown},
        {"ArrowLeftCtrl", Event::ArrowLeftCtrl},
        {"ArrowRightCtrl", Event::ArrowRightCtrl},
        {"ArrowUpCtrl", Event::ArrowUpCtrl},
        {"ArrowDownCtrl", Event::ArrowDownCtrl},
        {"Backspace", Event::Backspace},       {"Delete", Event::Delete},
        {"Return", Event::Return},             {"Escape", Event::Escape},
        {"Tab", Event::Tab},                   {"TabReverse", Event::TabReverse},
        {"Insert", Event::Insert},             {"Home", Event::Home},
        {"End", Event::End},                   {"PageUp", Event::PageUp},
        {"PageDown", Event::PageDown},         {"F1", Event::F1},
        {"F2", Event::F2},                     {"F3", Event::F3},
        {"F4", Event::F4},                     {"F5", Event::F5},
        {"F6", Event::F6},                     {"F7", Event::F7},
        {"F8", Event::F8},                     {"F9", Event::F9},
        {"F10", Event::F10},                   {"F11", Event::F11},
        {"F12", Event::F12},
    };
    // Ctrl+letter and Alt+letter (Tab/Return/Backspace above take precedence)
    for (char c = 'a'; c <= 'z'; c++) {
      std::string up(1, static_cast<char>(c - 'a' + 'A'));
      v.emplace_back("Ctrl" + up, Event::Special(std::string(1, static_cast<char>(c - 'a' + 1))));
      v.emplace_back("Alt" + up, Event::Special(std::string("\x1b") + c));
    }
    return v;
  }();
  return t;
}

const char* button_name(ftxui::Mouse::Button b) {
  switch (b) {
    case ftxui::Mouse::Left: return "left";
    case ftxui::Mouse::Middle: return "middle";
    case ftxui::Mouse::Right: return "right";
    case ftxui::Mouse::None: return "none";
    case ftxui::Mouse::WheelUp: return "wheel_up";
    case ftxui::Mouse::WheelDown: return "wheel_down";
    case ftxui::Mouse::WheelLeft: return "wheel_left";
    case ftxui::Mouse::WheelRight: return "wheel_right";
  }
  return "none";
}

}  // namespace

void push_event(lua_State* L, const ftxui::Event& ev_in) {
  ftxui::Event ev = ev_in;
  lua_newtable(L);
  const char* type = "special";
  if (ev.is_character()) type = "character";
  else if (ev.is_mouse()) type = "mouse";
  else if (ev.is_cursor_position() || ev.is_cursor_shape()) type = "other";
  lua_pushstring(L, type);
  lua_setfield(L, -2, "type");
  lua_pushlstring(L, ev.input().data(), ev.input().size());
  lua_setfield(L, -2, "input");
  if (ev.is_character()) {
    std::string c = ev.character();
    lua_pushlstring(L, c.data(), c.size());
    lua_setfield(L, -2, "character");
  }
  if (!ev.is_mouse() && !ev.is_character()) {
    for (const auto& kv : key_table()) {
      if (kv.second == ev) {
        lua_pushstring(L, kv.first.c_str());
        lua_setfield(L, -2, "key");
        break;
      }
    }
  }
  if (ev.is_mouse()) {
    const auto& m = ev.mouse();
    lua_newtable(L);
    lua_pushinteger(L, m.x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, m.y);
    lua_setfield(L, -2, "y");
    lua_pushstring(L, button_name(m.button));
    lua_setfield(L, -2, "button");
    lua_pushstring(L, m.motion == ftxui::Mouse::Pressed    ? "pressed"
                      : m.motion == ftxui::Mouse::Released ? "released"
                                                           : "moved");
    lua_setfield(L, -2, "motion");
    lua_pushboolean(L, m.shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, m.meta);
    lua_setfield(L, -2, "meta");
    lua_pushboolean(L, m.control);
    lua_setfield(L, -2, "control");
    lua_setfield(L, -2, "mouse");
  }
}

static ftxui::Event event_from_name(const std::string& s) {
  for (const auto& kv : key_table())
    if (kv.first == s) return kv.second;
  if (s == "Enter") return Event::Return;
  if (s == "Space") return Event::Character(' ');
  if (s.empty()) throw LuaError("empty event");
  return Event::Character(s);
}

ftxui::Event to_event(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  int t = lua_type(L, idx);
  if (t == LUA_TSTRING) return event_from_name(check_string(L, idx));
  if (t != LUA_TTABLE) arg_error(idx, "event must be a string or table");
  std::string s;
  lua_getfield(L, idx, "mouse");
  bool is_mouse = lua_istable(L, -1);
  if (is_mouse) {
    int m = lua_gettop(L);
    ftxui::Mouse mouse;
    field_int(L, m, "x", mouse.x);
    field_int(L, m, "y", mouse.y);
    mouse.button = ftxui::Mouse::Left;
    if (field_string(L, m, "button", s)) {
      static const std::map<std::string, ftxui::Mouse::Button> b = {
          {"left", ftxui::Mouse::Left},         {"middle", ftxui::Mouse::Middle},
          {"right", ftxui::Mouse::Right},       {"none", ftxui::Mouse::None},
          {"wheel_up", ftxui::Mouse::WheelUp},  {"wheel_down", ftxui::Mouse::WheelDown},
          {"wheel_left", ftxui::Mouse::WheelLeft},
          {"wheel_right", ftxui::Mouse::WheelRight}};
      auto it = b.find(s);
      if (it == b.end()) throw LuaError("unknown mouse button '" + s + "'");
      mouse.button = it->second;
    }
    mouse.motion = ftxui::Mouse::Pressed;
    if (field_string(L, m, "motion", s)) {
      if (s == "pressed") mouse.motion = ftxui::Mouse::Pressed;
      else if (s == "released") mouse.motion = ftxui::Mouse::Released;
      else if (s == "moved") mouse.motion = ftxui::Mouse::Moved;
      else throw LuaError("unknown mouse motion '" + s + "'");
    }
    field_bool(L, m, "shift", mouse.shift);
    field_bool(L, m, "meta", mouse.meta);
    field_bool(L, m, "control", mouse.control);
    lua_pop(L, 1);
    return Event::Mouse("", mouse);
  }
  lua_pop(L, 1);
  if (field_string(L, idx, "key", s)) return event_from_name(s);
  if (field_string(L, idx, "character", s)) return Event::Character(s);
  if (field_string(L, idx, "input", s)) return Event::Special(s);
  throw LuaError("event table needs 'key', 'character', 'input' or 'mouse'");
}

}  // namespace lf
