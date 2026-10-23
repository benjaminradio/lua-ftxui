# lua-ftxui

A native Lua module for building terminal user interfaces with
[FTXUI](https://github.com/ArthurSonzogni/FTXUI). The API is declarative and
table-based, in the spirit of [IUP](https://www.tecgraf.puc-rio.br/iup/):
widgets and layouts are nested constructor calls, options are attributes, and
behaviour is attached with `on_*` callbacks.

```lua
local ui = require("ftxui")

local name  = ui.input{ placeholder = "Your name" }
local greet = ui.button{ label = "Greet", on_click = function()
  message = "Hello, " .. (name.value ~= "" and name.value or "world") .. "!"
end }
local quit  = ui.button{ label = "Quit", on_click = ui.exit }

message = "Type a name and press Greet."
local form = ui.vertical{ name, ui.horizontal{ greet, quit } }

ui.run(ui.renderer(form, function()
  return ui.vbox{
    ui.text{ "lua-ftxui", bold = true, color = "cyan" },
    name:render():border(),
    ui.hbox{ greet:render(), quit:render() },
    ui.text(message):dim(),
    title = "Hello", border = "rounded",
  }
end), { mode = "fit" })
```

It is an ordinary Lua C module, so it works with any Lua 5.1 – 5.5 interpreter,
including the [LuaBinaries](https://luabinaries.sourceforge.net/download.html)
ones. FTXUI is linked statically; nothing else needs to be installed.

## Install

Download the archive matching your Lua version and platform from the
[Releases](../../releases) page and put `ftxui.dll` / `ftxui.so` where
`package.cpath` finds it (for example next to your script, with
`package.cpath = "./?.so;./?.dll;" .. package.cpath`).

| Asset | For |
|-------|-----|
| `lua-ftxui-lua5.X-windows-x64.zip` | 64-bit [LuaBinaries](https://luabinaries.sourceforge.net/download.html) `lua5X.dll` |
| `lua-ftxui-lua5.X-windows-x86.zip` | 32-bit LuaBinaries |
| `lua-ftxui-lua5.X-linux-x64.tar.gz` | Linux x86-64 (built on Ubuntu 22.04, glibc 2.35+) |

`X` is 1–5 and **must match the interpreter's Lua version**.

* **Windows:** the module imports the Lua API from `lua51.dll` … `lua55.dll`
  by name, so the interpreter must load that DLL (LuaBinaries' `lua5X.exe`
  does). It is built with the static MSVC runtime, so no redistributable is
  needed. *Lua 5.1 note:* the module imports `lua51.dll`; if your 5.1
  distribution only ships `lua5.1.dll`, copy or alias it to that name.
* **Linux:** the module has no link-time dependency on Lua and resolves the
  Lua API from the host executable, so the interpreter must export its symbols
  (distro `lua5.x` packages and the stock `make linux` build do).

> The CI builds and tests against stock Lua built from the lua.org sources with
> the LuaBinaries layout (`luaXY.dll` on Windows, an interpreter exporting its
> API on Linux). The release binaries have not been run against the actual
> LuaBinaries downloads.

Try the examples from the extracted directory: `lua examples/form.lua`.

## Concepts

FTXUI has two layers and so does this module:

* **Elements** are immutable, stateless pieces of layout (`ui.text`,
  `ui.hbox`, `ui.gauge`, …) that you decorate (`:bold()`, `:border()`, …).
  Build them anywhere; they only produce output when rendered.
* **Components** are stateful and interactive (`ui.button`, `ui.input`,
  `ui.menu`, …) and are arranged in containers (`ui.vertical`,
  `ui.horizontal`, `ui.tab`). A component draws itself with `comp:render()`,
  which returns an element, so the usual pattern is: declare components once,
  then give `ui.renderer` a function that lays their elements out.

`ui.run(root)` starts the event loop with a component as the root.

### Attributes and chaining

Every element constructor accepts decoration **attributes**, and each
decoration is also a **method** (and a plain function `ui.bold(e)`):

```lua
ui.text{ "Warning", bold = true, color = "yellow", border = "rounded", width = 20 }
ui.text("Warning"):bold():color("yellow"):border("rounded"):width(20)   -- same thing
```

Where it matters, attribute form applies in a fixed order (style → color →
size → flex → frame → border → title → centering); use method chaining when you
need a different nesting.

## Elements

| Constructor | |
|---|---|
| `ui.text(s)`, `ui.text{s, ...}` | single-line text (strings/numbers also work anywhere an element is expected) |
| `ui.vtext(s)` | vertical text |
| `ui.paragraph(s [, align])` | word-wrapped; align `left` `right` `center` `justify` |
| `ui.hbox{...}`, `ui.vbox{...}`, `ui.dbox{...}` | horizontal / vertical / overlaid layout; also accept varargs |
| `ui.hflow{...}`, `ui.vflow{...}` | wrapping flow layouts |
| `ui.flexbox{..., direction=, wrap=, justify=, align_items=, align_content=, gap_x=, gap_y=}` | CSS-like flexbox (`row` `column` `*_inversed`; `wrap` `nowrap`; `start` `end` `center` `stretch` `space_between` `space_around` `space_evenly`) |
| `ui.gridbox{ {a, b}, {c, d} }` | grid |
| `ui.table{ {..}, {..}, header=, separators=, style= }` | table; cells may be strings or elements; `style=false` removes borders |
| `ui.separator([style])`, `ui.filler()`, `ui.empty()` | divider, flexible gap, nothing |
| `ui.gauge(p [, "right"\|"left"\|"up"\|"down"])` | progress bar, `p` in 0..1 |
| `ui.spinner(frame [, charset])` | spinner frame (advance `frame` from a tick) |
| `ui.graph(function(w, h) return {ints} end)` | braille graph sampled at the given size |
| `ui.window(title, content [, style])` | titled frame |

Decorations (method / function / attribute):

| | |
|---|---|
| `bold` `dim` `italic` `inverted` `underlined` `underlined_double` `blink` `strikethrough` | text style (attribute value: `true`) |
| `color(c)` `bgcolor(c)` | colors, see below |
| `border([style [, color]])` | styles `light` `dashed` `heavy` `double` `rounded` `empty`; attribute may also be `{style=, color=}` |
| `title(text [, style])` | wrap in a titled window (attribute: `title="…"`) |
| `width(n)` `height(n)` `min_width` `max_width` `min_height` `max_height` | fixed / bounded size; method-only `size("width"\|"height", "equal"\|"min"\|"max", n)` |
| `flex` `flex_grow` `flex_shrink` `xflex` `yflex` (+ `_grow`/`_shrink`) `notflex` | flexibility |
| `hcenter` `vcenter` `center` `align_right` | alignment |
| `frame` `xframe` `yframe` `focus` `vscroll_indicator` `hscroll_indicator` | scrolling regions |
| `clear_under` `automerge` `hyperlink(url)` | misc |

**Colors** are one of: a name (`black red green yellow blue magenta cyan white`,
`gray`, `gray_light`, and `bright_*`/`*_light` variants, `default`), `"#rrggbb"`
or `"#rgb"`, a 0–255 palette index, or `{r, g, b}`. `ui.rgb(r,g,b)` and
`ui.hsv(h,s,v)` return `"#rrggbb"` strings. `ui.color_support([level])` reads or
forces the terminal capability (`"none"`, `"16"`, `"256"`, `"truecolor"`).

### Rendering without a terminal

```lua
print(ui.render(element))                        -- plain text, sized to fit
io.write(ui.render(element, { width = 40, color = true }), "\n")   -- with ANSI colors
print(element)                                   -- tostring() renders too
```

`ui.render(x [, {width=, height=, color=false}])` accepts elements, components
and strings. This is what the test-suite uses.

## Components

All constructors take one options table. Selection indices are **1-based**.
State is exposed as properties that can be read and assigned:

| Constructor | Options | Property |
|---|---|---|
| `ui.button{label, on_click, style}` or `ui.button(label, fn [, opts])` | `style`: `border` (default) `simple` `ascii` `animated` | – |
| `ui.input{value, placeholder, password, multiline, style, on_change, on_enter}` | `style`: `default` `spacious` | `.value`, `.placeholder` |
| `ui.checkbox{label, checked, on_change}` | | `.checked` |
| `ui.radiobox{entries, selected, on_change}` | | `.selected`, `.entries` |
| `ui.menu{entries, selected, direction, animated, on_change, on_enter}` | `direction`: `vertical` `horizontal` | `.selected`, `.entries` |
| `ui.toggle{entries, selected, on_change}` | | `.selected`, `.entries` |
| `ui.dropdown{entries, selected, on_change}` | | `.selected`, `.entries` |
| `ui.slider{label, value, min, max, step, on_change}` | | `.value` |
| `ui.vertical{...}` `ui.horizontal{...}` `ui.tab{..., selected=}` `ui.stacked{...}` | children are components | `.selected` |
| `ui.renderer(fn)` / `ui.renderer(fn, true)` / `ui.renderer(child, fn)` | `fn` returns an element; with `true` the component is focusable and `fn(focused)` is called | – |
| `ui.catch_event(child, fn)` | `fn(event)` returns `true` if it handled the event | – |
| `ui.maybe(child, fn)` | shown while `fn()` is truthy | – |
| `ui.modal(main, modal [, show])` | | `.show` |
| `ui.collapsible{label=, child, open=}` | | `.open` |
| `ui.resizable_split{main, back, direction=, size=}` | `direction`: `left` `right` `top` `bottom` | `.size` |

`on_change` / `on_enter` callbacks receive the new value (string, boolean,
1-based index or number). `.value` is accepted as an alias for the type's main
property.

Component methods: `c:render()`, `c:event(ev)`, `c:focus()`, `c:focused()`,
`c:active()`, `c:add(child)`, `c:detach()`, `c:child_count()`.

### Events

Callbacks of `ui.catch_event` get a table; `c:event(...)` and `ui.post_event(...)`
accept a string (`"a"`, `"ArrowUp"`, `"CtrlC"`, …) or a table:

```lua
{ type = "character", character = "a", input = "a" }
{ type = "special",   key = "ArrowUp", input = "\27[A" }   -- also Return Escape Tab TabReverse
                                                          -- Backspace Delete Insert Home End
                                                          -- PageUp PageDown F1..F12
                                                          -- CtrlA..CtrlZ AltA..AltZ Arrow*Ctrl
{ type = "mouse",     mouse = { x=, y=, button="left", motion="pressed",
                                shift=false, meta=false, control=false } }
```

### Running

```lua
ui.run(root, {
  mode = "fullscreen",   -- "alternate" "primary" "fit" "terminal" "fixed"(width=,height=)
  mouse = true,          -- track the mouse
  ctrl_c = true,         -- Ctrl+C exits
  tick = 0.1,            -- seconds between redraws (optional)
  on_tick = function() end,
})
ui.exit()                -- leave the loop (e.g. on_click = ui.exit)
ui.post_event(ev)        -- inject an event
```

`root` may be a component, an element, or a function returning an element.
`ui.run` blocks until `ui.exit()` or Ctrl+C. Callbacks always run on the
calling thread; `tick` only schedules them.

## Errors and memory

* An error raised in a callback (`on_click`, a renderer, …) stops the event
  loop, restores the terminal, and is re-raised from `ui.run` (or from
  `c:event` / `ui.render`) with a Lua traceback. Invalid arguments raise
  ordinary Lua errors (`bad argument #1 (…)`, `unknown color 'x'`).
* Components keep their state and callbacks alive for as long as anything in
  the tree references them, independently of Lua variables. A callback that
  captures the component it is attached to keeps that component alive for the
  life of the Lua state; this is harmless for a UI that lives as long as the
  program.

## Building from source

Requires CMake ≥ 3.20 and a C++17 compiler. FTXUI is fetched automatically.

```sh
cmake -S . -B build -DLUA_VERSION=5.4 -DLUA_INCLUDE_DIR=/usr/include/lua5.4
cmake --build build
lua tests/test.lua "./build/?.so"
```

On Windows pass `-DLUA_LIBRARY=<luaXY.lib>` (the import library of the Lua DLL)
as well. `tools/ci-build.sh 5.4` does everything CI does: it downloads stock
Lua, builds an interpreter laid out like LuaBinaries, builds the module against
it and runs the tests (the PTY tests run a real app in a pseudo-terminal and
need Python 3 on Linux).

## Releasing

Push a tag: `git tag v0.1.0 && git push origin v0.1.0`. The *Release* workflow
builds Linux x64 and Windows x64/x86 binaries for Lua 5.1–5.5, runs the tests
against each, and attaches the archives plus `SHA256SUMS.txt` to a GitHub
release. Running it manually (*Run workflow*) builds the artifacts without
publishing. The *CI* workflow runs the same build and tests for Linux and
Windows x64 on every pull request.

## Not (yet) wrapped

`canvas`, window (draggable) components, custom `transform` functions for
widgets, and FTXUI's selection/clipboard API. Contributions welcome.

## License

MIT, see [LICENSE](LICENSE). FTXUI is MIT-licensed too; its notice is in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
