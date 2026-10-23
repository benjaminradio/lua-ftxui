// Rendering to strings, the interactive loop, and small utilities.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include "core.hpp"

namespace lf {

using ftxui::Component;
using ftxui::Element;

namespace {

std::string plain_text(const ftxui::Screen& screen, int w, int h) {
  std::string out;
  for (int y = 0; y < h; y++) {
    std::string row;
    for (int x = 0; x < w; x++) {
      const std::string& c = screen.PixelAt(x, y).character;
      row += c.empty() ? " " : c;  // untouched pixels hold an empty string
    }
    size_t end = row.find_last_not_of(' ');
    row.erase(end == std::string::npos ? 0 : end + 1);
    if (y) out += '\n';
    out += row;
  }
  return out;
}

bool blank_row(const ftxui::Screen& screen, int w, int y) {
  for (int x = 0; x < w; x++) {
    const std::string& c = screen.PixelAt(x, y).character;
    if (!c.empty() && c != " ") return false;
  }
  return true;
}

std::string render_string(Element e, int width, int height, bool color) {
  using namespace ftxui;
  if (width > 0 && height <= 0) {
    // Fit's height does not account for wrapping at a given width, so grow the
    // canvas until the content no longer touches the bottom, then crop.
    for (int h = 32; h <= 4096; h *= 2) {
      Screen screen(width, h);
      Render(screen, e);
      if (!blank_row(screen, width, h - 1) || h * 2 > 4096) {
        int used = h;
        while (used > 1 && blank_row(screen, width, used - 1)) used--;
        if (color) {
          Screen cropped(width, used);
          Render(cropped, e);
          return cropped.ToString();
        }
        return plain_text(screen, width, used);
      }
    }
  }
  Dimensions fit = Dimension::Fit(e, true);
  int w = width > 0 ? width : fit.dimx;
  int h = height > 0 ? height : fit.dimy;
  Screen screen(w, h);
  Render(screen, e);
  return color ? screen.ToString() : plain_text(screen, w, h);
}

// ui.render(x [, {width=, height=, color=false}]) -> string
// Renders without a terminal: handy for tests, logs and static output.
int l_render(lua_State* L) {
  Element e = to_element(L, 1);
  int w = -1, h = -1;
  bool color = false;
  if (is_plain_table(L, 2)) {
    field_int(L, 2, "width", w);
    field_int(L, 2, "height", h);
    field_bool(L, 2, "color", color);
  }
  std::string s = render_string(e, w, h, color);
  lua_pushlstring(L, s.data(), s.size());
  return 1;
}

int e_tostring(lua_State* L) {
  Element e = to_element(L, 1);
  std::string s = render_string(e, -1, -1, false);
  lua_pushlstring(L, s.data(), s.size());
  return 1;
}

// Posts Event::Custom and an optional callback on a timer, from a helper
// thread; the callback itself always runs on the loop's thread.
class Ticker {
 public:
  Ticker(ftxui::ScreenInteractive* screen, double seconds, Fn on_tick)
      : thread_([this, screen, seconds, on_tick] {
          std::unique_lock<std::mutex> lock(mutex_);
          auto period = std::chrono::duration<double>(seconds);
          while (!cv_.wait_for(lock, period, [this] { return stop_; })) {
            screen->Post([on_tick] {
              if (on_tick) call_void(on_tick);
            });
            screen->PostEvent(ftxui::Event::Custom);
          }
        }) {}
  ~Ticker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::thread thread_;
};

struct RunOpts {
  bool mouse = true;
  bool ctrl_c = true;
  double tick = 0;
  Fn on_tick;
};

template <class Make>
void run_loop(Make make, const Component& root, const RunOpts& o) {
  auto screen = make();
  screen.TrackMouse(o.mouse);
  screen.ForceHandleCtrlC(o.ctrl_c);
  std::unique_ptr<Ticker> ticker;
  if (o.tick > 0) ticker.reset(new Ticker(&screen, o.tick, o.on_tick));
  screen.Loop(root);
  ticker.reset();  // join before the screen goes away
}

// ui.run(root [, opts]) where root is a component, an element or a function
// returning an element. Blocks until ui.exit() / Ctrl+C.
//   opts.mode   "fullscreen" (default) | "alternate" | "primary" | "fit" |
//               "terminal" | "fixed" (with width, height)
//   opts.mouse  track the mouse (default true)
//   opts.ctrl_c let FTXUI handle Ctrl+C by exiting (default true)
//   opts.tick   seconds between redraws; opts.on_tick() runs on each tick
int l_run(lua_State* L) {
  Component root;
  if (test_component(L, 1)) {
    root = check_component(L, 1);
  } else if (lua_type(L, 1) == LUA_TFUNCTION) {
    Fn f = check_fn(L, 1);
    root = ftxui::Renderer([f]() -> Element {
      Element r = ftxui::text("<error>");
      call_fn(f, nullptr, 1, [&r](lua_State* L) {
        try {
          r = to_element(L, -1);
        } catch (const std::exception& e) {
          record_error(std::string("run function result: ") + e.what());
        }
      });
      return r;
    });
  } else if (luaL_testudata(L, 1, kElementMT)) {
    Element e = to_element(L, 1);
    root = ftxui::Renderer([e] { return e; });
  } else {
    arg_error(1, "component, element or function expected");
  }

  RunOpts o;
  std::string mode = "fullscreen";
  int width = 80, height = 24;
  if (is_plain_table(L, 2)) {
    field_string(L, 2, "mode", mode);
    field_bool(L, 2, "mouse", o.mouse);
    field_bool(L, 2, "ctrl_c", o.ctrl_c);
    field_number(L, 2, "tick", o.tick);
    field_int(L, 2, "width", width);
    field_int(L, 2, "height", height);
    o.on_tick = opt_fn_field(L, 2, "on_tick");
  }
  using ftxui::ScreenInteractive;
  if (mode == "fullscreen") run_loop([] { return ScreenInteractive::Fullscreen(); }, root, o);
  else if (mode == "alternate") run_loop([] { return ScreenInteractive::FullscreenAlternateScreen(); }, root, o);
  else if (mode == "primary") run_loop([] { return ScreenInteractive::FullscreenPrimaryScreen(); }, root, o);
  else if (mode == "fit") run_loop([] { return ScreenInteractive::FitComponent(); }, root, o);
  else if (mode == "terminal") run_loop([] { return ScreenInteractive::TerminalOutput(); }, root, o);
  else if (mode == "fixed") run_loop([=] { return ScreenInteractive::FixedSize(width, height); }, root, o);
  else throw LuaError("run: unknown mode '" + mode + "'");
  return 0;
}

// ui.exit(): leave the running loop (no-op when none is running)
int l_exit(lua_State*) {
  if (auto* s = ftxui::ScreenInteractive::Active()) s->Exit();
  return 0;
}

// ui.post_event(event): inject an event into the running loop
int l_post_event(lua_State* L) {
  ftxui::Event ev = to_event(L, 1);
  if (auto* s = ftxui::ScreenInteractive::Active()) s->PostEvent(ev);
  return 0;
}

int l_terminal_size(lua_State* L) {
  auto d = ftxui::Terminal::Size();
  lua_pushinteger(L, d.dimx);
  lua_pushinteger(L, d.dimy);
  return 2;
}

int clamp255(lua_State* L, int i) {
  double d = check_number(L, i);
  return d < 0 ? 0 : d > 255 ? 255 : static_cast<int>(d);
}

std::string hex(int r, int g, int b) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x", r, g, b);
  return buf;
}

// ui.rgb(r, g, b) -> "#rrggbb"
int l_rgb(lua_State* L) {
  std::string s = hex(clamp255(L, 1), clamp255(L, 2), clamp255(L, 3));
  lua_pushstring(L, s.c_str());
  return 1;
}

// ui.hsv(h, s, v) -> "#rrggbb"   (all components 0..255, as in FTXUI)
int l_hsv(lua_State* L) {
  double h = clamp255(L, 1) / 255.0 * 6.0, s = clamp255(L, 2) / 255.0,
         v = clamp255(L, 3) / 255.0;
  int i = static_cast<int>(h) % 6;
  double f = h - static_cast<int>(h), p = v * (1 - s), q = v * (1 - f * s),
         t = v * (1 - (1 - f) * s);
  double r, g, b;
  switch (i) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
  }
  std::string out = hex(static_cast<int>(r * 255 + 0.5), static_cast<int>(g * 255 + 0.5),
                        static_cast<int>(b * 255 + 0.5));
  lua_pushstring(L, out.c_str());
  return 1;
}

// ui.color_support([level]) -> level
// Levels: "none", "16", "256", "truecolor". FTXUI detects this from the
// environment (COLORTERM, TERM); setting it forces the choice.
int l_color_support(lua_State* L) {
  if (!lua_isnoneornil(L, 1)) {
    std::string s = check_string(L, 1);
    if (s == "none") ftxui::Terminal::SetColorSupport(ftxui::Terminal::Palette1);
    else if (s == "16") ftxui::Terminal::SetColorSupport(ftxui::Terminal::Palette16);
    else if (s == "256") ftxui::Terminal::SetColorSupport(ftxui::Terminal::Palette256);
    else if (s == "truecolor") ftxui::Terminal::SetColorSupport(ftxui::Terminal::TrueColor);
    else throw LuaError("color_support: use 'none', '16', '256' or 'truecolor'");
  }
  switch (ftxui::Terminal::ColorSupport()) {
    case ftxui::Terminal::Palette1: lua_pushstring(L, "none"); break;
    case ftxui::Terminal::Palette16: lua_pushstring(L, "16"); break;
    case ftxui::Terminal::Palette256: lua_pushstring(L, "256"); break;
    default: lua_pushstring(L, "truecolor"); break;
  }
  return 1;
}

const luaL_Reg kFuncs[] = {
    {"color_support", guarded<l_color_support>},
    {"render", guarded<l_render>},
    {"run", guarded<l_run>},
    {"exit", guarded<l_exit>},
    {"post_event", guarded<l_post_event>},
    {"terminal_size", guarded<l_terminal_size>},
    {"rgb", guarded<l_rgb>},
    {"hsv", guarded<l_hsv>},
    {nullptr, nullptr},
};

}  // namespace

void register_screen(lua_State* L, int mod) {
  mod = lua_absindex(L, mod);
  luaL_setfuncs(L, kFuncs, 0);
  luaL_getmetatable(L, kElementMT);
  lua_pushcfunction(L, guarded<e_tostring>);
  lua_setfield(L, -2, "__tostring");
  lua_getfield(L, -1, "__index");
  lua_pushcfunction(L, guarded<l_render>);
  lua_setfield(L, -2, "render");
  lua_pop(L, 2);
}

}  // namespace lf
