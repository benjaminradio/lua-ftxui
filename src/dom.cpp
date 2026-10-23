// DOM elements: constructors, decorators (method form and attribute form).
#include <ftxui/dom/flexbox_config.hpp>
#include <ftxui/dom/table.hpp>

#include "core.hpp"

namespace lf {

using ftxui::Element;
using ftxui::Elements;

ftxui::BorderStyle to_border_style(const std::string& s) {
  if (s == "light") return ftxui::LIGHT;
  if (s == "dashed") return ftxui::DASHED;
  if (s == "heavy") return ftxui::HEAVY;
  if (s == "double") return ftxui::DOUBLE;
  if (s == "rounded") return ftxui::ROUNDED;
  if (s == "empty") return ftxui::EMPTY;
  throw LuaError("unknown border style '" + s +
                 "' (light, dashed, heavy, double, rounded, empty)");
}

namespace {

// ---- decorators ------------------------------------------------------------
//
// Each decorator is usable as a method (`e:bold()`, `e:color("red")`), as a
// function (`ui.bold(e)`) and, where `attr` is true, as a constructor
// attribute (`ui.text{"x", bold=true}`). `arg` is the stack index of the
// decorator's argument; it may lie beyond the top (LUA_TNONE) for methods
// called without one.

struct Spec {
  const char* name;
  Element (*fn)(Element, lua_State*, int arg);
  bool flag;  // attribute form is a boolean switch
  bool attr;  // usable as constructor attribute
};

#define FLAG(NAME, EXPR) \
  Element d_##NAME(Element e, lua_State*, int) { return EXPR; }

FLAG(bold, ftxui::bold(e))
FLAG(dim, ftxui::dim(e))
FLAG(italic, ftxui::italic(e))
FLAG(inverted, ftxui::inverted(e))
FLAG(underlined, ftxui::underlined(e))
FLAG(underlined_double, ftxui::underlinedDouble(e))
FLAG(blink, ftxui::blink(e))
FLAG(strikethrough, ftxui::strikethrough(e))
FLAG(flex, ftxui::flex(e))
FLAG(flex_grow, ftxui::flex_grow(e))
FLAG(flex_shrink, ftxui::flex_shrink(e))
FLAG(xflex, ftxui::xflex(e))
FLAG(xflex_grow, ftxui::xflex_grow(e))
FLAG(xflex_shrink, ftxui::xflex_shrink(e))
FLAG(yflex, ftxui::yflex(e))
FLAG(yflex_grow, ftxui::yflex_grow(e))
FLAG(yflex_shrink, ftxui::yflex_shrink(e))
FLAG(notflex, ftxui::notflex(e))
FLAG(hcenter, ftxui::hcenter(e))
FLAG(vcenter, ftxui::vcenter(e))
FLAG(center, ftxui::center(e))
FLAG(align_right, ftxui::align_right(e))
FLAG(frame, ftxui::frame(e))
FLAG(xframe, ftxui::xframe(e))
FLAG(yframe, ftxui::yframe(e))
FLAG(focus, ftxui::focus(e))
FLAG(vscroll_indicator, ftxui::vscroll_indicator(e))
FLAG(hscroll_indicator, ftxui::hscroll_indicator(e))
FLAG(clear_under, ftxui::clear_under(e))
FLAG(automerge, ftxui::automerge(e))

Element d_color(Element e, lua_State* L, int a) { return ftxui::color(to_color(L, a), e); }
Element d_bgcolor(Element e, lua_State* L, int a) { return ftxui::bgcolor(to_color(L, a), e); }

Element d_width(Element e, lua_State* L, int a) {
  return e | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, check_int(L, a));
}
Element d_height(Element e, lua_State* L, int a) {
  return e | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, check_int(L, a));
}
Element d_min_width(Element e, lua_State* L, int a) {
  return e | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, check_int(L, a));
}
Element d_max_width(Element e, lua_State* L, int a) {
  return e | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, check_int(L, a));
}
Element d_min_height(Element e, lua_State* L, int a) {
  return e | ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, check_int(L, a));
}
Element d_max_height(Element e, lua_State* L, int a) {
  return e | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, check_int(L, a));
}

// :size("width"|"height", "equal"|"min"|"max", n)
Element d_size(Element e, lua_State* L, int a) {
  std::string dim = check_string(L, a), con = check_string(L, a + 1);
  ftxui::WidthOrHeight wh;
  if (dim == "width") wh = ftxui::WIDTH;
  else if (dim == "height") wh = ftxui::HEIGHT;
  else throw LuaError("size: dimension must be 'width' or 'height'");
  ftxui::Constraint c;
  if (con == "equal") c = ftxui::EQUAL;
  else if (con == "min" || con == "greater_than") c = ftxui::GREATER_THAN;
  else if (con == "max" || con == "less_than") c = ftxui::LESS_THAN;
  else throw LuaError("size: constraint must be 'equal', 'min' or 'max'");
  return e | ftxui::size(wh, c, check_int(L, a + 2));
}

// border: nil|true -> light; "style"; {style=, color=}; method form also takes
// the color as a second argument.
Element d_border(Element e, lua_State* L, int a) {
  int t = lua_type(L, a);
  if (t == LUA_TBOOLEAN && !lua_toboolean(L, a)) return e;
  ftxui::BorderStyle st = ftxui::LIGHT;
  bool has_color = false;
  ftxui::Color col;
  if (t == LUA_TSTRING) st = to_border_style(check_string(L, a));
  if (t == LUA_TTABLE) {
    std::string s;
    if (field_string(L, a, "style", s)) st = to_border_style(s);
    lua_getfield(L, a, "color");
    if (!lua_isnil(L, -1)) {
      col = to_color(L, -1);
      has_color = true;
    }
    lua_pop(L, 1);
  } else if (!lua_isnoneornil(L, a + 1)) {
    col = to_color(L, a + 1);
    has_color = true;
  }
  return has_color ? (e | ftxui::borderStyled(st, col)) : (e | ftxui::borderStyled(st));
}

Element d_hyperlink(Element e, lua_State* L, int a) {
  return ftxui::hyperlink(check_string(L, a), e);
}

// :title("text" [, style]) wraps in a titled window
Element d_title(Element e, lua_State* L, int a) {
  ftxui::BorderStyle st = ftxui::ROUNDED;
  if (!lua_isnoneornil(L, a + 1)) st = to_border_style(check_string(L, a + 1));
  return ftxui::window(to_element(L, a), e, st);
}

// Order matters for attribute form: later entries wrap earlier ones.
const Spec kSpecs[] = {
    {"bold", d_bold, true, true},
    {"dim", d_dim, true, true},
    {"italic", d_italic, true, true},
    {"inverted", d_inverted, true, true},
    {"underlined", d_underlined, true, true},
    {"underlined_double", d_underlined_double, true, true},
    {"blink", d_blink, true, true},
    {"strikethrough", d_strikethrough, true, true},
    {"color", d_color, false, true},
    {"bgcolor", d_bgcolor, false, true},
    {"hyperlink", d_hyperlink, false, true},
    {"width", d_width, false, true},
    {"height", d_height, false, true},
    {"min_width", d_min_width, false, true},
    {"max_width", d_max_width, false, true},
    {"min_height", d_min_height, false, true},
    {"max_height", d_max_height, false, true},
    {"size", d_size, false, false},
    {"flex", d_flex, true, true},
    {"flex_grow", d_flex_grow, true, true},
    {"flex_shrink", d_flex_shrink, true, true},
    {"xflex", d_xflex, true, true},
    {"xflex_grow", d_xflex_grow, true, true},
    {"xflex_shrink", d_xflex_shrink, true, true},
    {"yflex", d_yflex, true, true},
    {"yflex_grow", d_yflex_grow, true, true},
    {"yflex_shrink", d_yflex_shrink, true, true},
    {"notflex", d_notflex, true, true},
    {"frame", d_frame, true, true},
    {"xframe", d_xframe, true, true},
    {"yframe", d_yframe, true, true},
    {"focus", d_focus, true, true},
    {"vscroll_indicator", d_vscroll_indicator, true, true},
    {"hscroll_indicator", d_hscroll_indicator, true, true},
    {"clear_under", d_clear_under, true, true},
    {"automerge", d_automerge, true, true},
    {"border", d_border, false, true},
    {"title", d_title, false, false},  // attribute form handled in apply_attrs
    {"hcenter", d_hcenter, true, true},
    {"vcenter", d_vcenter, true, true},
    {"center", d_center, true, true},
    {"align_right", d_align_right, true, true},
};
constexpr int kSpecCount = sizeof(kSpecs) / sizeof(kSpecs[0]);

int deco_impl(lua_State* L) {
  const Spec& s = kSpecs[lua_tointeger(L, lua_upvalueindex(1))];
  Element e = to_element(L, 1);
  push_element(L, s.fn(std::move(e), L, 2));
  return 1;
}

}  // namespace

Element apply_attrs(lua_State* L, Element e, int attrs) {
  if (attrs == 0 || !lua_istable(L, attrs)) return e;
  attrs = lua_absindex(L, attrs);
  std::string title;
  bool has_title = field_string(L, attrs, "title", title);
  for (int i = 0; i < kSpecCount; i++) {
    const Spec& s = kSpecs[i];
    if (!s.attr) continue;
    if (has_title && std::string(s.name) == "border") continue;  // window draws it
    lua_getfield(L, attrs, s.name);
    if (!lua_isnil(L, -1)) {
      if (!s.flag || lua_toboolean(L, -1)) e = s.fn(std::move(e), L, lua_gettop(L));
    }
    lua_pop(L, 1);
  }
  if (has_title) {
    ftxui::BorderStyle st = ftxui::ROUNDED;
    lua_getfield(L, attrs, "border");
    if (lua_type(L, -1) == LUA_TSTRING) st = to_border_style(check_string(L, -1));
    lua_pop(L, 1);
    e = ftxui::window(ftxui::text(title), e, st);
  }
  return e;
}

namespace {

// Index of a trailing attribute table among the arguments, or 0.
int trailing_attrs(lua_State* L) {
  int n = lua_gettop(L);
  return (n >= 1 && is_plain_table(L, n)) ? n : 0;
}

int l_text(lua_State* L) {
  std::string s;
  int attrs = 0;
  if (is_plain_table(L, 1)) {
    lua_rawgeti(L, 1, 1);
    s = check_string(L, -1);
    lua_pop(L, 1);
    attrs = 1;
  } else {
    s = check_string(L, 1);
    attrs = trailing_attrs(L);
  }
  push_element(L, apply_attrs(L, ftxui::text(s), attrs));
  return 1;
}

int l_vtext(lua_State* L) {
  push_element(L, apply_attrs(L, ftxui::vtext(check_string(L, 1)), trailing_attrs(L)));
  return 1;
}

int l_paragraph(lua_State* L) {
  std::string s;
  int attrs = 0;
  if (is_plain_table(L, 1)) {
    lua_rawgeti(L, 1, 1);
    s = check_string(L, -1);
    lua_pop(L, 1);
    attrs = 1;
  } else {
    s = check_string(L, 1);
    attrs = trailing_attrs(L);
  }
  std::string align = "left";
  if (attrs) field_string(L, attrs, "align", align);
  else if (lua_type(L, 2) == LUA_TSTRING) align = check_string(L, 2);
  Element e;
  if (align == "left") e = ftxui::paragraphAlignLeft(s);
  else if (align == "right") e = ftxui::paragraphAlignRight(s);
  else if (align == "center") e = ftxui::paragraphAlignCenter(s);
  else if (align == "justify") e = ftxui::paragraphAlignJustify(s);
  else throw LuaError("paragraph: align must be left, right, center or justify");
  push_element(L, apply_attrs(L, e, attrs));
  return 1;
}

// ui.hbox{a, b, c, attr=...} or ui.hbox(a, b, c)
int make_box(lua_State* L, Element (*make)(Elements)) {
  Elements kids;
  int attrs = 0;
  if (is_plain_table(L, 1)) {
    kids = to_elements(L, 1);
    attrs = 1;
  } else {
    for (int i = 1, n = lua_gettop(L); i <= n; i++) kids.push_back(to_element(L, i));
  }
  push_element(L, apply_attrs(L, make(std::move(kids)), attrs));
  return 1;
}

int l_hbox(lua_State* L) { return make_box(L, ftxui::hbox); }
int l_vbox(lua_State* L) { return make_box(L, ftxui::vbox); }
int l_dbox(lua_State* L) { return make_box(L, ftxui::dbox); }
int l_hflow(lua_State* L) { return make_box(L, ftxui::hflow); }
int l_vflow(lua_State* L) { return make_box(L, ftxui::vflow); }

int l_flexbox(lua_State* L) {
  using C = ftxui::FlexboxConfig;
  if (!is_plain_table(L, 1)) arg_error(1, "table expected");
  C cfg;
  std::string s;
  if (field_string(L, 1, "direction", s)) {
    if (s == "row") cfg.Set(C::Direction::Row);
    else if (s == "row_inversed") cfg.Set(C::Direction::RowInversed);
    else if (s == "column") cfg.Set(C::Direction::Column);
    else if (s == "column_inversed") cfg.Set(C::Direction::ColumnInversed);
    else throw LuaError("flexbox: bad direction '" + s + "'");
  }
  if (field_string(L, 1, "wrap", s)) {
    if (s == "wrap") cfg.Set(C::Wrap::Wrap);
    else if (s == "nowrap") cfg.Set(C::Wrap::NoWrap);
    else if (s == "wrap_inversed") cfg.Set(C::Wrap::WrapInversed);
    else throw LuaError("flexbox: bad wrap '" + s + "'");
  }
  if (field_string(L, 1, "justify", s)) {
    if (s == "start") cfg.Set(C::JustifyContent::FlexStart);
    else if (s == "end") cfg.Set(C::JustifyContent::FlexEnd);
    else if (s == "center") cfg.Set(C::JustifyContent::Center);
    else if (s == "stretch") cfg.Set(C::JustifyContent::Stretch);
    else if (s == "space_between") cfg.Set(C::JustifyContent::SpaceBetween);
    else if (s == "space_around") cfg.Set(C::JustifyContent::SpaceAround);
    else if (s == "space_evenly") cfg.Set(C::JustifyContent::SpaceEvenly);
    else throw LuaError("flexbox: bad justify '" + s + "'");
  }
  if (field_string(L, 1, "align_items", s)) {
    if (s == "start") cfg.Set(C::AlignItems::FlexStart);
    else if (s == "end") cfg.Set(C::AlignItems::FlexEnd);
    else if (s == "center") cfg.Set(C::AlignItems::Center);
    else if (s == "stretch") cfg.Set(C::AlignItems::Stretch);
    else throw LuaError("flexbox: bad align_items '" + s + "'");
  }
  if (field_string(L, 1, "align_content", s)) {
    if (s == "start") cfg.Set(C::AlignContent::FlexStart);
    else if (s == "end") cfg.Set(C::AlignContent::FlexEnd);
    else if (s == "center") cfg.Set(C::AlignContent::Center);
    else if (s == "stretch") cfg.Set(C::AlignContent::Stretch);
    else if (s == "space_between") cfg.Set(C::AlignContent::SpaceBetween);
    else if (s == "space_around") cfg.Set(C::AlignContent::SpaceAround);
    else if (s == "space_evenly") cfg.Set(C::AlignContent::SpaceEvenly);
    else throw LuaError("flexbox: bad align_content '" + s + "'");
  }
  int gx = 0, gy = 0;
  field_int(L, 1, "gap_x", gx);
  field_int(L, 1, "gap_y", gy);
  cfg.SetGap(gx, gy);
  push_element(L, apply_attrs(L, ftxui::flexbox(to_elements(L, 1), cfg), 1));
  return 1;
}

// ui.gridbox{ {a, b}, {c, d} }
int l_gridbox(lua_State* L) {
  if (!is_plain_table(L, 1)) arg_error(1, "table of rows expected");
  std::vector<Elements> rows;
  for (int i = 1, n = static_cast<int>(lua_rawlen(L, 1)); i <= n; i++) {
    lua_rawgeti(L, 1, i);
    if (!is_plain_table(L, -1)) arg_error(1, "gridbox rows must be tables");
    rows.push_back(to_elements(L, -1));
    lua_pop(L, 1);
  }
  push_element(L, apply_attrs(L, ftxui::gridbox(std::move(rows)), 1));
  return 1;
}

int l_separator(lua_State* L) {
  Element e;
  if (lua_type(L, 1) == LUA_TSTRING) {
    std::string s = check_string(L, 1);
    if (s == "empty") e = ftxui::separatorEmpty();
    else e = ftxui::separatorStyled(to_border_style(s));
  } else {
    e = ftxui::separator();
  }
  push_element(L, apply_attrs(L, e, trailing_attrs(L)));
  return 1;
}

int l_filler(lua_State* L) {
  push_element(L, apply_attrs(L, ftxui::filler(), trailing_attrs(L)));
  return 1;
}

int l_empty(lua_State* L) {
  push_element(L, apply_attrs(L, ftxui::emptyElement(), trailing_attrs(L)));
  return 1;
}

// ui.gauge(0.5 [, "right"|"left"|"up"|"down"] [, attrs])
int l_gauge(lua_State* L) {
  float p = static_cast<float>(check_number(L, 1));
  if (p < 0.f) p = 0.f;
  if (p > 1.f) p = 1.f;
  ftxui::Direction d = ftxui::Direction::Right;
  if (lua_type(L, 2) == LUA_TSTRING) {
    std::string s = check_string(L, 2);
    if (s == "right") d = ftxui::Direction::Right;
    else if (s == "left") d = ftxui::Direction::Left;
    else if (s == "up") d = ftxui::Direction::Up;
    else if (s == "down") d = ftxui::Direction::Down;
    else throw LuaError("gauge: direction must be right, left, up or down");
  }
  push_element(L, apply_attrs(L, ftxui::gaugeDirection(p, d), trailing_attrs(L)));
  return 1;
}

// ui.spinner(frame [, charset])
int l_spinner(lua_State* L) {
  int frame = check_int(L, 1);
  int charset = lua_type(L, 2) == LUA_TNUMBER ? check_int(L, 2) : 15;
  if (frame < 0) frame = 0;
  push_element(L, apply_attrs(L, ftxui::spinner(charset, static_cast<size_t>(frame)),
                              trailing_attrs(L)));
  return 1;
}

// ui.window(title, content [, style] [, attrs])
int l_window(lua_State* L) {
  Element title = to_element(L, 1), content = to_element(L, 2);
  ftxui::BorderStyle st = ftxui::ROUNDED;
  if (lua_type(L, 3) == LUA_TSTRING) st = to_border_style(check_string(L, 3));
  push_element(L, apply_attrs(L, ftxui::window(title, content, st), trailing_attrs(L)));
  return 1;
}

// ui.table{ {"a","b"}, {"c","d"}, header=true, style="light", separators=true }
int l_table(lua_State* L) {
  if (!is_plain_table(L, 1)) arg_error(1, "table of rows expected");
  std::vector<Elements> rows;
  for (int i = 1, n = static_cast<int>(lua_rawlen(L, 1)); i <= n; i++) {
    lua_rawgeti(L, 1, i);
    if (!is_plain_table(L, -1)) arg_error(1, "table rows must be tables");
    rows.push_back(to_elements(L, -1));
    lua_pop(L, 1);
  }
  if (rows.empty()) throw LuaError("table needs at least one row");
  bool header = false, separators = false, has_style = true;
  field_bool(L, 1, "header", header);
  field_bool(L, 1, "separators", separators);
  ftxui::BorderStyle st = ftxui::LIGHT;
  lua_getfield(L, 1, "style");
  if (lua_type(L, -1) == LUA_TSTRING) st = to_border_style(check_string(L, -1));
  else if (lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1)) has_style = false;
  lua_pop(L, 1);
  ftxui::Table t(std::move(rows));
  if (has_style) {
    t.SelectAll().Border(st);
    if (separators) t.SelectAll().Separator(st);
  }
  if (header) {
    t.SelectRow(0).Decorate(ftxui::bold);
    if (has_style) t.SelectRow(0).Border(st);
  }
  push_element(L, apply_attrs(L, t.Render(), 1));
  return 1;
}

// ui.graph(function(width, height) return {ints...} end [, attrs])
int l_graph(lua_State* L) {
  Fn f = check_fn(L, 1);
  auto fn = [f](int w, int h) {
    std::vector<int> out;
    call_fn(
        f,
        [w, h](lua_State* L) {
          lua_pushinteger(L, w);
          lua_pushinteger(L, h);
          return 2;
        },
        1,
        [&out](lua_State* L) {
          if (!lua_istable(L, -1)) return;
          for (int i = 1, n = static_cast<int>(lua_rawlen(L, -1)); i <= n; i++) {
            lua_rawgeti(L, -1, i);
            out.push_back(static_cast<int>(lua_tonumber(L, -1)));
            lua_pop(L, 1);
          }
        });
    return out;
  };
  push_element(L, apply_attrs(L, ftxui::graph(fn), trailing_attrs(L)));
  return 1;
}

const luaL_Reg kCtors[] = {
    {"text", guarded<l_text>},       {"vtext", guarded<l_vtext>},
    {"paragraph", guarded<l_paragraph>},
    {"hbox", guarded<l_hbox>},       {"vbox", guarded<l_vbox>},
    {"dbox", guarded<l_dbox>},       {"hflow", guarded<l_hflow>},
    {"vflow", guarded<l_vflow>},     {"flexbox", guarded<l_flexbox>},
    {"gridbox", guarded<l_gridbox>}, {"separator", guarded<l_separator>},
    {"filler", guarded<l_filler>},   {"empty", guarded<l_empty>},
    {"gauge", guarded<l_gauge>},     {"spinner", guarded<l_spinner>},
    {"window", guarded<l_window>},   {"table", guarded<l_table>},
    {"graph", guarded<l_graph>},     {nullptr, nullptr},
};

}  // namespace

void register_dom(lua_State* L, int mod) {
  mod = lua_absindex(L, mod);
  luaL_setfuncs(L, kCtors, 0);  // into the table on top (the module)
  // methods table for Element handles
  luaL_getmetatable(L, kElementMT);
  lua_newtable(L);
  for (int i = 0; i < kSpecCount; i++) {
    lua_pushinteger(L, i);
    lua_pushcclosure(L, guarded<deco_impl>, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, kSpecs[i].name);  // method
    lua_setfield(L, mod, kSpecs[i].name);  // ui.<name>(element, ...)
  }
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
}

}  // namespace lf
