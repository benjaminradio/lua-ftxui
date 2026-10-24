// Shared plumbing for the Lua <-> FTXUI binding.
//
// Error model: Lua errors (longjmp) must never unwind through C++ frames that
// own objects. Binding functions therefore signal problems by throwing
// lf::LuaError (or any std::exception); `guarded<F>` catches it, releases all
// C++ state and only then raises the Lua error. Callbacks into Lua always use
// lua_pcall; a failure is stored and re-raised once control is back in the
// outermost binding function (and the interactive loop is asked to exit).
#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "compat.hpp"

namespace lf {

struct LuaError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// ---- state & error plumbing ------------------------------------------------

extern lua_State* g_L;            // state of the innermost binding call
extern std::string g_pending;     // error raised inside a Lua callback

void set_state(lua_State* L);     // also releases deferred registry refs
void record_error(const std::string& msg);

// Runs f; converts C++ exceptions / pending callback errors into an error
// message left on top of the Lua stack and returns -1 (otherwise f's result).
// It never raises a Lua error itself: `guarded` does that from a frame that
// owns no C++ objects, so the longjmp behind lua_error only ever unwinds
// frames without exception-handling data (matters on MSVC, where longjmp
// unwinds through C++ frames and the module and Lua use different runtimes).
int guard_call(lua_State* L, int (*f)(lua_State*));

template <int (*F)(lua_State*)>
int guarded(lua_State* L) {
  int n = guard_call(L, F);
  if (n < 0) return lua_error(L);
  return n;
}

// ---- Lua function references ----------------------------------------------

class LuaRef {
 public:
  LuaRef(lua_State* L, int idx);
  ~LuaRef();  // defers luaL_unref: the owning state may not be at hand
  LuaRef(const LuaRef&) = delete;
  LuaRef& operator=(const LuaRef&) = delete;
  void push(lua_State* L) const;

 private:
  int ref_;
};
using Fn = std::shared_ptr<LuaRef>;

Fn check_fn(lua_State* L, int idx);                    // throws unless function
Fn opt_fn_field(lua_State* L, int tbl, const char* k);  // nullptr when nil

// Calls f. `push` pushes the arguments and returns their count. `take`, when
// given, sees the first result at -1 (stack is restored afterwards). Returns
// false if the call failed (error recorded).
bool call_fn(const Fn& f, const std::function<int(lua_State*)>& push, int nres,
             const std::function<void(lua_State*)>& take);
void call_void(const Fn& f);
void call_void(const Fn& f, const std::function<int(lua_State*)>& push);
bool call_bool(const Fn& f, const std::function<int(lua_State*)>& push);

// ---- argument helpers (throw LuaError) --------------------------------------

[[noreturn]] void arg_error(int idx, const std::string& what);
std::string check_string(lua_State* L, int idx);
std::string opt_string(lua_State* L, int idx, const std::string& def);
double check_number(lua_State* L, int idx);
int check_int(lua_State* L, int idx);
bool is_plain_table(lua_State* L, int idx);  // table, not userdata

// table fields; all take an absolute table index
bool field_present(lua_State* L, int tbl, const char* k);
bool field_string(lua_State* L, int tbl, const char* k, std::string& out);
bool field_number(lua_State* L, int tbl, const char* k, double& out);
bool field_int(lua_State* L, int tbl, const char* k, int& out);
bool field_bool(lua_State* L, int tbl, const char* k, bool& out);  // truthy test

// colors: name, "#rgb"/"#rrggbb", 0..255 palette index, or {r,g,b}
ftxui::Color to_color(lua_State* L, int idx);

// ---- userdata ---------------------------------------------------------------

constexpr const char* kElementMT = "ftxui.Element";
constexpr const char* kComponentMT = "ftxui.Component";

struct ElemBox {
  ftxui::Element e;
};

// State shared between a Lua handle and the C++ component it drives. FTXUI
// widgets bind to raw pointers into this, so it must live as long as the
// component does (see make_component).
struct Model {
  enum Kind { None, Str, Bool, Index, Int, Float } kind = None;
  const char* alias = "";  // property name usable instead of `value`
  std::string s;
  std::string placeholder;
  bool b = false;
  int i = 0;  // Index kind: stored 0-based, exposed 1-based
  float f = 0.f;
  std::vector<std::string> entries;
  bool has_entries = false;
  bool has_placeholder = false;
};

struct CompBox {
  ftxui::Component c;
  std::shared_ptr<Model> m;
};

void push_element(lua_State* L, ftxui::Element e);
void push_component(lua_State* L, ftxui::Component c, std::shared_ptr<Model> m);
ftxui::Element to_element(lua_State* L, int idx);      // elem | comp | string | number
ftxui::Elements to_elements(lua_State* L, int tbl);    // array part of a table
ftxui::Component check_component(lua_State* L, int idx);
CompBox* test_component(lua_State* L, int idx);

// Keeps `model` alive for as long as the returned component is.
ftxui::Component make_component(ftxui::Component inner, std::shared_ptr<Model> m);

// events
void push_event(lua_State* L, const ftxui::Event& ev);
ftxui::Event to_event(lua_State* L, int idx);

// ---- registration -----------------------------------------------------------

void register_dom(lua_State* L, int mod);        // elements & decorators
void register_components(lua_State* L, int mod);  // interactive widgets
void register_screen(lua_State* L, int mod);      // render / run / misc
void init_metatables(lua_State* L);

ftxui::Element apply_attrs(lua_State* L, ftxui::Element e, int attrs_tbl);
ftxui::BorderStyle to_border_style(const std::string& s);

}  // namespace lf
