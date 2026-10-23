// Interactive components (FTXUI "component" layer).
#include <cstdio>

#include <ftxui/component/component_options.hpp>

#include "core.hpp"

namespace lf {

using ftxui::Component;
using ftxui::Components;
using ftxui::Element;

namespace {

using ModelPtr = std::shared_ptr<Model>;

// ---- model <-> Lua -------------------------------------------------------------

void push_value(lua_State* L, const Model& m) {
  switch (m.kind) {
    case Model::Str: lua_pushlstring(L, m.s.data(), m.s.size()); break;
    case Model::Bool: lua_pushboolean(L, m.b); break;
    case Model::Index: lua_pushinteger(L, m.i + 1); break;
    case Model::Int: lua_pushinteger(L, m.i); break;
    case Model::Float: lua_pushnumber(L, m.f); break;
    case Model::None: lua_pushnil(L); break;
  }
}

bool is_value_key(const Model& m, const std::string& k) {
  return m.kind != Model::None && (k == "value" || k == m.alias);
}

bool get_prop(lua_State* L, const Model& m, const std::string& k) {
  if (is_value_key(m, k)) {
    push_value(L, m);
    return true;
  }
  if (m.has_placeholder && k == "placeholder") {
    lua_pushlstring(L, m.placeholder.data(), m.placeholder.size());
    return true;
  }
  if (m.has_entries && k == "entries") {
    lua_createtable(L, static_cast<int>(m.entries.size()), 0);
    for (size_t i = 0; i < m.entries.size(); i++) {
      lua_pushlstring(L, m.entries[i].data(), m.entries[i].size());
      lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return true;
  }
  return false;
}

std::vector<std::string> read_entries(lua_State* L, int tbl) {
  tbl = lua_absindex(L, tbl);
  if (!lua_istable(L, tbl)) throw LuaError("entries must be an array of strings");
  std::vector<std::string> out;
  for (int i = 1, n = static_cast<int>(lua_rawlen(L, tbl)); i <= n; i++) {
    lua_rawgeti(L, tbl, i);
    int t = lua_type(L, -1);
    if (t != LUA_TSTRING && t != LUA_TNUMBER) {
      lua_pop(L, 1);
      throw LuaError("entries must be an array of strings");
    }
    out.push_back(check_string(L, -1));
    lua_pop(L, 1);
  }
  return out;
}

void set_prop(lua_State* L, Model& m, const std::string& k, int v) {
  if (is_value_key(m, k)) {
    switch (m.kind) {
      case Model::Str: m.s = check_string(L, v); break;
      case Model::Bool: m.b = lua_toboolean(L, v) != 0; break;
      case Model::Index: m.i = std::max(0, check_int(L, v) - 1); break;
      case Model::Int: m.i = check_int(L, v); break;
      case Model::Float: m.f = static_cast<float>(check_number(L, v)); break;
      case Model::None: break;
    }
    return;
  }
  if (m.has_placeholder && k == "placeholder") {
    m.placeholder = check_string(L, v);
    return;
  }
  if (m.has_entries && k == "entries") {
    m.entries = read_entries(L, v);
    return;
  }
  throw LuaError("cannot set '" + k + "' on this component");
}

// Callback helper: `fn(value)` with the model's current value.
std::function<void()> value_callback(const Fn& f, const ModelPtr& m) {
  if (!f) return [] {};
  return [f, m] { call_void(f, [m](lua_State* L) { push_value(L, *m); return 1; }); };
}

// Calls f and converts its result to an element; reports misuse as an error.
Element call_element(const Fn& f, const std::function<int(lua_State*)>& push) {
  Element r = ftxui::text("<error>");
  call_fn(f, push, 1, [&r](lua_State* L) {
    if (lua_isnil(L, -1)) {
      record_error("render function returned nil (expected an element)");
      return;
    }
    try {
      r = to_element(L, -1);
    } catch (const std::exception& e) {
      record_error(std::string("render function result: ") + e.what());
    }
  });
  return r;
}

Components read_children(lua_State* L, int tbl) {
  tbl = lua_absindex(L, tbl);
  Components out;
  for (int i = 1, n = static_cast<int>(lua_rawlen(L, tbl)); i <= n; i++) {
    lua_rawgeti(L, tbl, i);
    out.push_back(check_component(L, -1));
    lua_pop(L, 1);
  }
  return out;
}

int finish(lua_State* L, Component inner, ModelPtr m) {
  Component c = make_component(std::move(inner), m);
  push_component(L, c, m);
  return 1;
}

ModelPtr new_model(Model::Kind kind, const char* alias) {
  auto m = std::make_shared<Model>();
  m->kind = kind;
  m->alias = alias;
  return m;
}

void need_table(lua_State* L, int idx) {
  if (!is_plain_table(L, idx)) arg_error(idx, "options table expected");
}

void read_selected(lua_State* L, int tbl, Model& m) {
  int sel = 1;
  field_int(L, tbl, "selected", sel);
  m.i = std::max(0, sel - 1);
}

// ---- widgets -------------------------------------------------------------------

// ui.button{label=, on_click=, style="border"|"simple"|"ascii"|"animated"}
// ui.button("Label", fn [, opts])
int l_button(lua_State* L) {
  std::string label = "Button", style = "border";
  Fn on_click;
  if (lua_type(L, 1) == LUA_TSTRING) {
    label = check_string(L, 1);
    if (!lua_isnoneornil(L, 2)) on_click = check_fn(L, 2);
    if (is_plain_table(L, 3)) field_string(L, 3, "style", style);
  } else {
    need_table(L, 1);
    field_string(L, 1, "label", label);
    field_string(L, 1, "style", style);
    on_click = opt_fn_field(L, 1, "on_click");
  }
  ftxui::ButtonOption o;
  if (style == "border") o = ftxui::ButtonOption::Border();
  else if (style == "simple") o = ftxui::ButtonOption::Simple();
  else if (style == "ascii") o = ftxui::ButtonOption::Ascii();
  else if (style == "animated") o = ftxui::ButtonOption::Animated();
  else throw LuaError("button: unknown style '" + style + "'");
  o.label = label;
  if (on_click) o.on_click = [on_click] { call_void(on_click); };
  return finish(L, ftxui::Button(o), new_model(Model::None, ""));
}

// ui.input{value=, placeholder=, password=, multiline=, on_change=, on_enter=}
int l_input(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Str, "value");
  m->has_placeholder = true;
  field_string(L, 1, "value", m->s);
  field_string(L, 1, "placeholder", m->placeholder);
  bool password = false, multiline = false;
  field_bool(L, 1, "password", password);
  field_bool(L, 1, "multiline", multiline);
  std::string style = "default";
  field_string(L, 1, "style", style);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  Fn on_enter = opt_fn_field(L, 1, "on_enter");
  ftxui::InputOption o =
      style == "spacious" ? ftxui::InputOption::Spacious() : ftxui::InputOption::Default();
  if (style != "default" && style != "spacious")
    throw LuaError("input: unknown style '" + style + "'");
  o.content = &m->s;
  o.placeholder = &m->placeholder;
  o.password = password;
  o.multiline = multiline;
  o.on_change = value_callback(on_change, m);
  o.on_enter = value_callback(on_enter, m);
  return finish(L, ftxui::Input(o), m);
}

// ui.checkbox{label=, checked=, on_change=}
int l_checkbox(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Bool, "checked");
  std::string label = "Checkbox";
  field_string(L, 1, "label", label);
  field_bool(L, 1, "checked", m->b);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  ftxui::CheckboxOption o = ftxui::CheckboxOption::Simple();
  o.label = label;
  o.checked = &m->b;
  o.on_change = value_callback(on_change, m);
  return finish(L, ftxui::Checkbox(o), m);
}

// ui.radiobox{entries={...}, selected=1, on_change=}
int l_radiobox(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Index, "selected");
  m->has_entries = true;
  lua_getfield(L, 1, "entries");
  m->entries = read_entries(L, -1);
  lua_pop(L, 1);
  read_selected(L, 1, *m);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  ftxui::RadioboxOption o = ftxui::RadioboxOption::Simple();
  o.entries = &m->entries;
  o.selected = &m->i;
  o.on_change = value_callback(on_change, m);
  return finish(L, ftxui::Radiobox(o), m);
}

// ui.menu{entries=, selected=, direction="vertical"|"horizontal", animated=,
//         on_change=, on_enter=}
int l_menu(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Index, "selected");
  m->has_entries = true;
  lua_getfield(L, 1, "entries");
  m->entries = read_entries(L, -1);
  lua_pop(L, 1);
  read_selected(L, 1, *m);
  std::string dir = "vertical";
  bool animated = false;
  field_string(L, 1, "direction", dir);
  field_bool(L, 1, "animated", animated);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  Fn on_enter = opt_fn_field(L, 1, "on_enter");
  ftxui::MenuOption o;
  if (dir == "vertical")
    o = animated ? ftxui::MenuOption::VerticalAnimated() : ftxui::MenuOption::Vertical();
  else if (dir == "horizontal")
    o = animated ? ftxui::MenuOption::HorizontalAnimated() : ftxui::MenuOption::Horizontal();
  else
    throw LuaError("menu: direction must be 'vertical' or 'horizontal'");
  o.entries = &m->entries;
  o.selected = &m->i;
  o.on_change = value_callback(on_change, m);
  o.on_enter = value_callback(on_enter, m);
  return finish(L, ftxui::Menu(o), m);
}

// ui.toggle{entries=, selected=, on_change=}
int l_toggle(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Index, "selected");
  m->has_entries = true;
  lua_getfield(L, 1, "entries");
  m->entries = read_entries(L, -1);
  lua_pop(L, 1);
  read_selected(L, 1, *m);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  ftxui::MenuOption o = ftxui::MenuOption::Toggle();
  o.entries = &m->entries;
  o.selected = &m->i;
  o.on_change = value_callback(on_change, m);
  return finish(L, ftxui::Menu(o), m);
}

// ui.dropdown{entries=, selected=, open=, on_change=}
int l_dropdown(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Index, "selected");
  m->has_entries = true;
  lua_getfield(L, 1, "entries");
  m->entries = read_entries(L, -1);
  lua_pop(L, 1);
  read_selected(L, 1, *m);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  ftxui::DropdownOption o;
  o.radiobox.entries = &m->entries;
  o.radiobox.selected = &m->i;
  o.radiobox.on_change = value_callback(on_change, m);
  return finish(L, ftxui::Dropdown(o), m);
}

// ui.slider{label=, value=, min=, max=, step=, on_change=}
int l_slider(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Float, "value");
  std::string label;
  double v = 0, mn = 0, mx = 100, step = 1;
  field_string(L, 1, "label", label);
  field_number(L, 1, "min", mn);
  field_number(L, 1, "max", mx);
  v = mn;
  field_number(L, 1, "value", v);
  step = (mx - mn) / 20.0;
  field_number(L, 1, "step", step);
  if (!(mx > mn)) throw LuaError("slider: max must be greater than min");
  if (!(step > 0)) throw LuaError("slider: step must be positive");
  m->f = static_cast<float>(v);
  Fn on_change = opt_fn_field(L, 1, "on_change");
  ftxui::SliderOption<float> o;
  o.value = &m->f;
  o.min = static_cast<float>(mn);
  o.max = static_cast<float>(mx);
  o.increment = static_cast<float>(step);
  o.on_change = value_callback(on_change, m);
  Component slider = ftxui::Slider(o);
  Component c = ftxui::Renderer(slider, [slider, m, label] {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(m->f));
    return ftxui::hbox({
        label.empty() ? ftxui::emptyElement() : ftxui::text(label + " "),
        slider->Render() | ftxui::xflex,
        ftxui::text(std::string(" ") + buf),
    });
  });
  return finish(L, c, m);
}

// ---- containers & wrappers ---------------------------------------------------------

int container(lua_State* L, int kind) {
  need_table(L, 1);
  Components kids = read_children(L, 1);
  auto m = new_model(Model::Index, "selected");
  read_selected(L, 1, *m);
  Component c;
  switch (kind) {
    case 0: c = ftxui::Container::Vertical(kids, &m->i); break;
    case 1: c = ftxui::Container::Horizontal(kids, &m->i); break;
    case 2: c = ftxui::Container::Tab(kids, &m->i); break;
    default: c = ftxui::Container::Stacked(kids); break;
  }
  return finish(L, c, m);
}
int l_vertical(lua_State* L) { return container(L, 0); }
int l_horizontal(lua_State* L) { return container(L, 1); }
int l_tab(lua_State* L) { return container(L, 2); }
int l_stacked(lua_State* L) { return container(L, 3); }

// ui.renderer(fn)              non-focusable
// ui.renderer(fn, true)        focusable; fn(focused)
// ui.renderer(child, fn)       draws `child` with custom layout
int l_renderer(lua_State* L) {
  auto m = new_model(Model::None, "");
  if (test_component(L, 1)) {
    Component child = check_component(L, 1);
    Fn f = check_fn(L, 2);
    return finish(L, ftxui::Renderer(child, [f] { return call_element(f, nullptr); }), m);
  }
  Fn f = check_fn(L, 1);
  if (lua_toboolean(L, 2)) {
    std::function<Element(bool)> fn = [f](bool focused) {
      return call_element(f, [focused](lua_State* L) {
        lua_pushboolean(L, focused);
        return 1;
      });
    };
    return finish(L, ftxui::Renderer(fn), m);
  }
  std::function<Element()> fn = [f] { return call_element(f, nullptr); };
  return finish(L, ftxui::Renderer(fn), m);
}

// ui.catch_event(child, function(event) return handled end)
int l_catch_event(lua_State* L) {
  Component child = check_component(L, 1);
  Fn f = check_fn(L, 2);
  auto c = ftxui::CatchEvent(child, [f](ftxui::Event ev) {
    return call_bool(f, [ev](lua_State* L) {
      push_event(L, ev);
      return 1;
    });
  });
  return finish(L, c, new_model(Model::None, ""));
}

// ui.maybe(child, function() return visible end)
int l_maybe(lua_State* L) {
  Component child = check_component(L, 1);
  Fn f = check_fn(L, 2);
  auto c = ftxui::Maybe(child, [f] { return call_bool(f, nullptr); });
  return finish(L, c, new_model(Model::None, ""));
}

// ui.modal(main, modal [, show]) -- `.show` toggles the modal
int l_modal(lua_State* L) {
  Component main = check_component(L, 1), modal = check_component(L, 2);
  auto m = new_model(Model::Bool, "show");
  m->b = lua_toboolean(L, 3) != 0;
  return finish(L, ftxui::Modal(main, modal, &m->b), m);
}

// ui.collapsible{label=, child, open=}
int l_collapsible(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Bool, "open");
  std::string label = "Collapsible";
  field_string(L, 1, "label", label);
  field_bool(L, 1, "open", m->b);
  lua_getfield(L, 1, "child");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_rawgeti(L, 1, 1);
  }
  Component child = check_component(L, -1);
  lua_pop(L, 1);
  return finish(L, ftxui::Collapsible(label, child, &m->b), m);
}

// ui.resizable_split{main=, back=, direction="left"|"right"|"top"|"bottom", size=}
int l_resizable_split(lua_State* L) {
  need_table(L, 1);
  auto m = new_model(Model::Int, "size");
  ftxui::ResizableSplitOption o;
  lua_getfield(L, 1, "main");
  o.main = check_component(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, 1, "back");
  o.back = check_component(L, -1);
  lua_pop(L, 1);
  std::string dir = "left";
  field_string(L, 1, "direction", dir);
  if (dir == "left") o.direction = ftxui::Direction::Left;
  else if (dir == "right") o.direction = ftxui::Direction::Right;
  else if (dir == "top") o.direction = ftxui::Direction::Up;
  else if (dir == "bottom") o.direction = ftxui::Direction::Down;
  else throw LuaError("resizable_split: bad direction '" + dir + "'");
  m->i = (dir == "left" || dir == "right") ? 20 : 10;
  field_int(L, 1, "size", m->i);
  o.main_size = &m->i;
  return finish(L, ftxui::ResizableSplit(o), m);
}

// ---- handle methods & properties ----------------------------------------------------

int m_render(lua_State* L) {
  push_element(L, check_component(L, 1)->Render());
  return 1;
}

int m_event(lua_State* L) {
  Component c = check_component(L, 1);
  lua_pushboolean(L, c->OnEvent(to_event(L, 2)));
  return 1;
}

int m_focus(lua_State* L) {
  check_component(L, 1)->TakeFocus();
  return 0;
}

int m_focused(lua_State* L) {
  lua_pushboolean(L, check_component(L, 1)->Focused());
  return 1;
}

int m_active(lua_State* L) {
  lua_pushboolean(L, check_component(L, 1)->Active());
  return 1;
}

int m_add(lua_State* L) {
  Component c = check_component(L, 1);
  c->Add(check_component(L, 2));
  lua_settop(L, 1);
  return 1;
}

int m_detach(lua_State* L) {
  check_component(L, 1)->Detach();
  return 0;
}

int m_child_count(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(check_component(L, 1)->ChildCount()));
  return 1;
}

int mt_index(lua_State* L) {
  lua_pushvalue(L, 2);
  lua_rawget(L, lua_upvalueindex(1));
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);
  CompBox* b = test_component(L, 1);
  if (b && b->m && lua_type(L, 2) == LUA_TSTRING && get_prop(L, *b->m, check_string(L, 2)))
    return 1;
  lua_pushnil(L);
  return 1;
}

int mt_newindex(lua_State* L) {
  CompBox* b = test_component(L, 1);
  if (!b) arg_error(1, "component expected");
  std::string k = check_string(L, 2);
  if (!b->m) throw LuaError("cannot set '" + k + "' on this component");
  set_prop(L, *b->m, k, 3);
  return 0;
}

int mt_tostring(lua_State* L) {
  lua_pushfstring(L, "ftxui.Component (%p)", lua_topointer(L, 1));
  return 1;
}

const luaL_Reg kCtors[] = {
    {"button", guarded<l_button>},
    {"input", guarded<l_input>},
    {"checkbox", guarded<l_checkbox>},
    {"radiobox", guarded<l_radiobox>},
    {"menu", guarded<l_menu>},
    {"toggle", guarded<l_toggle>},
    {"dropdown", guarded<l_dropdown>},
    {"slider", guarded<l_slider>},
    {"vertical", guarded<l_vertical>},
    {"horizontal", guarded<l_horizontal>},
    {"tab", guarded<l_tab>},
    {"stacked", guarded<l_stacked>},
    {"renderer", guarded<l_renderer>},
    {"catch_event", guarded<l_catch_event>},
    {"maybe", guarded<l_maybe>},
    {"modal", guarded<l_modal>},
    {"collapsible", guarded<l_collapsible>},
    {"resizable_split", guarded<l_resizable_split>},
    {nullptr, nullptr},
};

const luaL_Reg kMethods[] = {
    {"render", guarded<m_render>},
    {"event", guarded<m_event>},
    {"focus", guarded<m_focus>},
    {"focused", guarded<m_focused>},
    {"active", guarded<m_active>},
    {"add", guarded<m_add>},
    {"detach", guarded<m_detach>},
    {"child_count", guarded<m_child_count>},
    {nullptr, nullptr},
};

}  // namespace

void register_components(lua_State* L, int mod) {
  mod = lua_absindex(L, mod);
  luaL_setfuncs(L, kCtors, 0);
  luaL_getmetatable(L, kComponentMT);
  lua_newtable(L);
  luaL_setfuncs(L, kMethods, 0);
  lua_pushvalue(L, -1);
  lua_pushcclosure(L, guarded<mt_index>, 1);
  lua_setfield(L, -3, "__index");
  lua_pop(L, 1);  // methods table (the closure holds its own reference)
  lua_pushcfunction(L, guarded<mt_newindex>);
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, guarded<mt_tostring>);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}

}  // namespace lf
