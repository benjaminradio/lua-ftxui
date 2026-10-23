-- Headless test-suite: renders to strings and drives components with
-- synthetic events, so it needs no terminal.
--
--   lua tests/test.lua "./build/?.so"        (cpath template for the module)

if arg and arg[1] then package.cpath = arg[1] .. ";" .. package.cpath end
local ui = require("ftxui")
ui.color_support("truecolor")  -- make colour escapes independent of the environment

local passed, failed = 0, 0
local function test(name, fn)
  local ok, err = xpcall(fn, debug.traceback)
  if ok then
    passed = passed + 1
  else
    failed = failed + 1
    print("FAIL  " .. name .. "\n" .. tostring(err) .. "\n")
  end
end

local function eq(a, b, msg)
  if a ~= b then
    error(("%s\n  expected: %s\n  actual:   %s"):format(msg or "values differ", tostring(b), tostring(a)), 2)
  end
end
local function has(s, sub, msg)
  if not s:find(sub, 1, true) then
    error(("%s\n  missing: %s\n  in:\n%s"):format(msg or "substring not found", sub, s), 2)
  end
end
local function fails(fn, pattern)
  local ok, err = pcall(fn)
  if ok then error("expected an error matching '" .. pattern .. "'", 2) end
  if not tostring(err):find(pattern) then
    error(("error '%s' does not match '%s'"):format(tostring(err), pattern), 2)
  end
end

---------------------------------------------------------------- module
test("module metadata", function()
  eq(type(ui.version), "string")
  eq(type(ui.ftxui_version), "string")
  eq(type(ui.run), "function")
end)

---------------------------------------------------------------- elements
test("text renders", function()
  eq(ui.render(ui.text("hello")), "hello")
  eq(tostring(ui.text("hello")), "hello")
  eq(ui.text("hi"):render(), "hi")
  eq(ui.render("plain string"), "plain string")
end)

test("text table form with attributes", function()
  local out = ui.render(ui.text{ "x", bold = true, color = "red" }, { color = true })
  has(out, "\27[1m", "bold escape")
  has(out, "\27[31m", "red escape")
  eq(ui.render(ui.text{ "x", bold = true }), "x")
end)

test("hbox / vbox accept strings and varargs", function()
  eq(ui.render(ui.hbox{ "a", "b", "c" }), "abc")
  eq(ui.render(ui.hbox("a", "b")), "ab")
  eq(ui.render(ui.vbox{ "a", "b" }), "a\nb")
end)

test("borders", function()
  eq(ui.render(ui.text("x"):border()), "┌─┐\n│x│\n└─┘")
  eq(ui.render(ui.text("x"):border("rounded")), "╭─╮\n│x│\n╰─╯")
  eq(ui.render(ui.text("x"):border("double")), "╔═╗\n║x║\n╚═╝")
  eq(ui.render(ui.text("x"):border("heavy")), "┏━┓\n┃x┃\n┗━┛")
  eq(ui.render(ui.text{ "x", border = "rounded" }), "╭─╮\n│x│\n╰─╯")
  eq(ui.render(ui.text{ "x", border = false }), "x")
  has(ui.render(ui.text("x"):border("light", "red"), { color = true }), "\27[31m")
end)

test("title wraps in a window", function()
  local out = ui.render(ui.text{ "body", title = "Hi" })
  has(out, "Hi")
  has(out, "body")
  eq(select(2, out:gsub("\n", "")), 2, "three rows")
  eq(ui.render(ui.text("body"):title("Hi")), out)
  eq(ui.render(ui.window("Hi", "body")), out)
end)

test("size / width / height", function()
  eq(ui.render(ui.text("x"):width(5), { color = false }), "x")
  local out = ui.render(ui.text("x"):width(5):border())
  eq(out, "┌─────┐\n│x    │\n└─────┘")
  out = ui.render(ui.text("x"):size("width", "equal", 3):size("height", "equal", 2):border())
  eq(out, "┌───┐\n│x  │\n│   │\n└───┘")
  eq(ui.render(ui.text{ "x", width = 3, height = 2, border = true }), out)
end)

test("decorator functions mirror methods", function()
  eq(ui.render(ui.border(ui.text("x"))), ui.render(ui.text("x"):border()))
  eq(ui.render(ui.bold(ui.text("x")), { color = true }), ui.render(ui.text("x"):bold(), { color = true }))
end)

test("separator, filler and gauge", function()
  eq(ui.render(ui.hbox{ "a", ui.separator(), "b" }), "a│b")
  eq(ui.render(ui.vbox{ "a", ui.separator(), "b" }, { width = 3 }), "a\n───\nb")
  eq(ui.render(ui.hbox{ "a", ui.filler(), "b" }, { width = 6 }), "a    b")
  eq(ui.render(ui.gauge(0), { width = 4 }):find("█", 1, true), nil)
  has(ui.render(ui.gauge(1), { width = 4 }), "████")
  has(ui.render(ui.gauge(2), { width = 4 }), "████", "progress is clamped")
end)

test("paragraph wraps", function()
  local out = ui.render(ui.paragraph("one two three four"), { width = 9 })
  eq(out, "one two\nthree\nfour")
  has(ui.render(ui.paragraph("a b", "right"), { width = 5 }), "a b")
  fails(function() ui.paragraph("x", "nope") end, "align")
end)

test("gridbox, flexbox, flows", function()
  eq(ui.render(ui.gridbox{ { "a", "b" }, { "c", "d" } }), "ab\ncd")
  eq(ui.render(ui.flexbox{ "a", "b", "c", direction = "column" }), "a\nb\nc")
  eq(ui.render(ui.hflow{ "aa", "bb", "cc" }, { width = 4 }), "aabb\ncc")
  eq(ui.render(ui.vflow{ "a", "b" }), "a\nb")
  fails(function() ui.flexbox{ "a", direction = "sideways" } end, "direction")
end)

test("dbox overlays", function()
  eq(ui.render(ui.dbox{ "aaa", "b" }), "baa")
end)

test("table", function()
  local out = ui.render(ui.table{ { "a", "b" }, { "c", "d" } })
  eq(out, "┌──┐\n│ab│\n│cd│\n└──┘")
  has(ui.render(ui.table{ { "a", "b" }, { "c", "d" }, separators = true }), "├─┼─┤")
  eq(ui.render(ui.table{ { "a" }, style = false }), "a")
  has(ui.render(ui.table{ { "h1", "h2" }, { "1", "2" }, header = true }), "h1")
  fails(function() ui.table{} end, "at least one row")
end)

test("graph calls back with the size", function()
  local w, h
  local out = ui.render(ui.graph(function(width, height)
    w, h = width, height
    local t = {}
    for i = 1, width do t[i] = (i % height) end
    return t
  end), { width = 10, height = 4 })
  eq(w, 20); eq(h, 8)  -- braille sub-pixels: the graph is sampled at 2x horizontally
  eq(#out > 0, true)
end)

test("spinner", function()
  eq(ui.render(ui.spinner(0)) ~= ui.render(ui.spinner(1)), true)
end)

test("colors", function()
  local function colored(c) return ui.render(ui.text("x"):color(c), { color = true }) end
  has(colored("red"), "\27[31m")
  has(colored("bright_red"), "\27[91m")
  has(colored("#ff0000"), "\27[38;2;255;0;0m")
  has(colored("#f00"), "\27[38;2;255;0;0m")
  has(colored({ 0, 255, 0 }), "\27[38;2;0;255;0m")
  has(colored({ r = 0, g = 0, b = 255 }), "\27[38;2;0;0;255m")
  has(colored(196), "\27[38;5;196m")
  eq(ui.color_support(), "truecolor")
  has(colored(ui.rgb(1, 2, 3)), "\27[38;2;1;2;3m")
  eq(ui.rgb(255, 0, 16), "#ff0010")
  eq(ui.hsv(0, 255, 255), "#ff0000")
  has(ui.render(ui.text("x"):bgcolor("blue"), { color = true }), "\27[44m")
  fails(function() ui.text("x"):color("chartreuse-ish") end, "unknown color")
  fails(function() ui.text("x"):color("#12") end, "invalid color")
  fails(function() ui.text("x"):color(300) end, "palette")
  fails(function() ui.text("x"):color(true) end, "color must be")
end)

test("argument errors carry position and message", function()
  fails(function() ui.text() end, "bad argument #1")
  fails(function() ui.text("x"):border("zigzag") end, "unknown border style")
  fails(function() ui.vbox{ {} } end, "element, component or string expected")
  fails(function() ui.hbox{ 1, true } end, "element, component or string expected")
  fails(function() ui.render(ui.text("x"), "oops") ; error("unreachable-ok") end, "unreachable%-ok")
  local ok, err = pcall(ui.text)
  eq(ok, false)
  eq(type(err), "string")
end)

---------------------------------------------------------------- events
test("event tables", function()
  local seen
  local r = ui.catch_event(ui.renderer(function() return ui.text("x") end), function(ev)
    seen = ev
    return true
  end)
  eq(r:event("a"), true)
  eq(seen.type, "character"); eq(seen.character, "a"); eq(seen.input, "a")
  r:event("ArrowUp")
  eq(seen.type, "special"); eq(seen.key, "ArrowUp")
  r:event("Return"); eq(seen.key, "Return")
  r:event("Tab"); eq(seen.key, "Tab")
  r:event("CtrlC"); eq(seen.key, "CtrlC")
  r:event("AltX"); eq(seen.key, "AltX")
  r:event("F5"); eq(seen.key, "F5")
  r:event("Space"); eq(seen.character, " ")
  r:event({ character = "é" }); eq(seen.character, "é")
  r:event({ key = "Escape" }); eq(seen.key, "Escape")
  r:event({ mouse = { x = 3, y = 4, button = "right", motion = "released", shift = true } })
  eq(seen.type, "mouse")
  eq(seen.mouse.x, 3); eq(seen.mouse.y, 4)
  eq(seen.mouse.button, "right"); eq(seen.mouse.motion, "released")
  eq(seen.mouse.shift, true); eq(seen.mouse.control, false)
  fails(function() r:event({}) end, "event table needs")
  fails(function() r:event({ mouse = { button = "laser" } }) end, "unknown mouse button")
  fails(function() r:event(42) end, "string or table")
end)

---------------------------------------------------------------- components
test("button click", function()
  local clicks = 0
  local b = ui.button{ label = "Go", on_click = function() clicks = clicks + 1 end }
  has(ui.render(b), "Go")
  b:event("Return")
  eq(clicks, 1)
  ui.render(b)  -- mouse hit-testing needs a rendered layout
  b:event({ mouse = { x = 1, y = 1, button = "left", motion = "pressed" } })
  local b2 = ui.button("Positional", function() clicks = clicks + 10 end, { style = "simple" })
  b2:event("Return")
  eq(clicks, 12, "the mouse click and the Return key both fire on_click")
  eq(ui.render(ui.button{ label = "A", style = "ascii" }), "[A]")
  eq(ui.render(ui.button{ label = "A", style = "simple" }), "┌─┐\n│A│\n└─┘")
  fails(function() ui.button{ style = "round" } end, "unknown style")
  fails(function() ui.button{ on_click = 5 } end, "must be a function")
end)

test("input editing", function()
  local changes, enters = {}, {}
  local i = ui.input{
    placeholder = "name",
    on_change = function(v) changes[#changes + 1] = v end,
    on_enter = function(v) enters[#enters + 1] = v end,
  }
  has(ui.render(i), "name", "placeholder shown")
  i:event("h"); i:event("i")
  eq(i.value, "hi")
  eq(table.concat(changes, ","), "h,hi")
  i:event("Backspace")
  eq(i.value, "h")
  i:event("Return")
  eq(enters[1], "h")
  i.value = "set from lua"
  has(ui.render(i), "set from lua")
  eq(i.placeholder, "name")
  i.placeholder = "other"
  eq(i.placeholder, "other")
  local p = ui.input{ value = "secret", password = true }
  eq(ui.render(p):find("secret", 1, true), nil)
  has(ui.render(p), "••••••")
  fails(function() ui.input{ value = {} } end, "must be a string")
  fails(function() i.value = {} end, "string expected")
end)

test("checkbox", function()
  local last
  local c = ui.checkbox{ label = "Opt", on_change = function(v) last = v end }
  eq(c.checked, false)
  has(ui.render(c), "☐ Opt")
  c:event("Return")
  eq(c.checked, true); eq(last, true)
  has(ui.render(c), "▣ Opt")
  c:event("Space")
  eq(c.checked, false); eq(last, false)
  c.checked = true
  eq(c.value, true)
  has(ui.render(c), "▣")
end)

test("radiobox", function()
  local last
  local r = ui.radiobox{ entries = { "a", "b", "c" }, selected = 2, on_change = function(v) last = v end }
  eq(r.selected, 2)
  r:event("ArrowDown"); r:event("Return")
  eq(r.selected, 3); eq(last, 3)
  has(ui.render(r), "◉ c")
  r.selected = 1
  has(ui.render(r), "◉ a")
  eq(#r.entries, 3)
  r.entries = { "x", "y" }
  eq(r.entries[2], "y")
  has(ui.render(r), "x")
  fails(function() ui.radiobox{ entries = { {} } } end, "array of strings")
  fails(function() ui.radiobox{} end, "array of strings")
end)

test("menu / toggle / dropdown", function()
  local entered
  local m = ui.menu{ entries = { "one", "two", "three" }, on_enter = function(v) entered = v end }
  m:event("ArrowDown")
  eq(m.selected, 2)
  m:event("Return"); eq(entered, 2)
  local hm = ui.menu{ entries = { "a", "b" }, direction = "horizontal" }
  hm:event("ArrowRight"); eq(hm.selected, 2)
  fails(function() ui.menu{ entries = { "a" }, direction = "diagonal" } end, "direction")

  local t = ui.toggle{ entries = { "on", "off" } }
  t:event("ArrowRight"); eq(t.selected, 2)
  has(ui.render(t), "off")

  local d = ui.dropdown{ entries = { "red", "green", "blue" } }
  has(ui.render(d), "red")
  d:event("Return")  -- opens
  has(ui.render(d), "blue")
  d:event("ArrowDown"); d:event("Return")
  eq(d.selected, 2)
  has(ui.render(d), "green")
end)

test("slider", function()
  local last
  local s = ui.slider{ label = "Vol", min = 0, max = 10, value = 5, step = 1, on_change = function(v) last = v end }
  eq(s.value, 5)
  has(ui.render(s, { width = 30 }), "Vol")
  s:event("ArrowRight")
  eq(s.value, 6); eq(last, 6)
  s:event("ArrowLeft"); s:event("ArrowLeft")
  eq(s.value, 4)
  s.value = 9
  has(ui.render(s, { width = 30 }), "9")
  fails(function() ui.slider{ min = 5, max = 5 } end, "max must be greater")
  fails(function() ui.slider{ step = 0 } end, "step must be positive")
end)

test("containers route focus", function()
  local a = ui.input{ placeholder = "a" }
  local b = ui.input{ placeholder = "b" }
  local v = ui.vertical{ a, b }
  v:event("x")
  eq(a.value, "x"); eq(b.value, "")
  v:event("ArrowDown")
  v:event("y")
  eq(a.value, "x"); eq(b.value, "y")
  eq(v.selected, 2)
  v.selected = 1
  v:event("z")
  eq(a.value, "xz")
  eq(v:child_count(), 2)

  local h = ui.horizontal{ ui.input{}, ui.input{} }
  h:event("ArrowRight")
  eq(h.selected, 2)

  local tab = ui.tab{ a, b, selected = 2 }
  tab:event("q")
  eq(b.value, "yq")
  tab.selected = 1
  tab:event("w")
  eq(a.value, "xzw")
  fails(function() ui.vertical{ "nope" } end, "component expected")
end)

test("renderer composes components", function()
  local btn = ui.button{ label = "B", style = "ascii" }
  local inp = ui.input{ value = "v" }
  local root = ui.renderer(ui.vertical{ btn, inp }, function()
    return ui.vbox{ ui.text("title"), btn:render(), inp:render() }
  end)
  eq(ui.render(root), "title\n[B]\nv")
  local r2 = ui.renderer(function() return "just text" end)
  eq(ui.render(r2), "just text")
  local seen
  local r3 = ui.renderer(function(focused) seen = focused; return ui.text("f") end, true)
  ui.render(r3)
  eq(seen, true)
  fails(function() ui.render(ui.renderer(function() return nil end)) end, "returned nil")
  fails(function() ui.render(ui.renderer(function() return {} end)) end, "render function result")
end)

test("catch_event / maybe / modal / collapsible", function()
  local inp = ui.input{}
  local quit = false
  local c = ui.catch_event(inp, function(ev)
    if ev.character == "q" then quit = true; return true end
  end)
  c:event("q")
  eq(quit, true); eq(inp.value, "")
  c:event("w"); eq(inp.value, "w")

  local visible = false
  local m = ui.maybe(ui.renderer(function() return "shown" end), function() return visible end)
  eq(ui.render(m), "")
  visible = true
  eq(ui.render(m), "shown")

  local main = ui.renderer(function() return "main" end)
  local dlg = ui.renderer(function() return ui.text("dialog"):border() end)
  local modal = ui.modal(main, dlg)
  eq(modal.show, false)
  eq(ui.render(modal, { width = 20, height = 5 }):find("dialog", 1, true), nil)
  modal.show = true
  has(ui.render(modal, { width = 20, height = 5 }), "dialog")

  local col = ui.collapsible{ label = "More", ui.renderer(function() return "inner" end) }
  eq(col.open, false)
  eq(ui.render(col):find("inner", 1, true), nil)
  col.open = true
  has(ui.render(col), "inner")
end)

test("resizable_split", function()
  local l = ui.renderer(function() return "L" end)
  local r = ui.renderer(function() return "R" end)
  local s = ui.resizable_split{ main = l, back = r, size = 6 }
  eq(s.size, 6)
  has(ui.render(s, { width = 20, height = 1 }), "L")
  fails(function() ui.resizable_split{ main = l, back = r, direction = "up" } end, "bad direction")
end)

test("unknown properties", function()
  local b = ui.button{ label = "x" }
  eq(b.value, nil)
  fails(function() b.value = 1 end, "cannot set")
  fails(function() b.nonsense = 1 end, "cannot set")
  eq(tostring(b):match("^ftxui%.Component"), "ftxui.Component")
end)

---------------------------------------------------------------- errors in Lua callbacks
test("errors in callbacks propagate with traceback", function()
  local b = ui.button{ label = "x", on_click = function() error("boom from click") end }
  local ok, err = pcall(b.event, b, "Return")
  eq(ok, false)
  has(err, "boom from click")
  has(err, "stack traceback", "traceback attached")
  -- and the module keeps working afterwards
  eq(ui.render(ui.text("still ok")), "still ok")

  local r = ui.renderer(function() error("boom from render") end)
  local ok2, err2 = pcall(ui.render, r)
  eq(ok2, false)
  has(err2, "boom from render")
  eq(ui.render(ui.text("fine")), "fine")
end)

test("callbacks may call back into the module", function()
  local inner
  local b = ui.button{ label = "x", on_click = function() inner = ui.render(ui.text("nested")) end }
  b:event("Return")
  eq(inner, "nested")
end)

---------------------------------------------------------------- lifetime
test("components and elements survive garbage collection", function()
  local keep = {}
  for i = 1, 200 do
    local inp = ui.input{ value = "v" .. i, on_change = function() end }
    local root = ui.vertical{ inp, ui.checkbox{ label = "c" }, ui.radiobox{ entries = { "a", "b" } } }
    keep[#keep + 1] = root
    inp = nil
  end
  collectgarbage(); collectgarbage()
  for _, c in ipairs(keep) do
    c:event("x")
    eq(#ui.render(c) > 0, true)
  end
  keep = nil
  collectgarbage(); collectgarbage()
  -- a child outlives the Lua handle of its parent and of itself
  local parent
  do
    local child = ui.input{ value = "kept" }
    parent = ui.vertical{ child }
  end
  collectgarbage(); collectgarbage()
  has(ui.render(parent), "kept")
  parent:event("!")
  has(ui.render(parent), "!kept")
end)

---------------------------------------------------------------- misc
test("terminal_size", function()
  local w, h = ui.terminal_size()
  eq(type(w), "number"); eq(type(h), "number")
end)

test("exit / post_event without a running loop are harmless", function()
  ui.exit()
  ui.post_event("a")
end)

test("run rejects bad arguments before touching the terminal", function()
  fails(function() ui.run(42) end, "component, element or function expected")
  fails(function() ui.run("text") end, "component, element or function expected")
  fails(function() ui.run(ui.text("x"), { mode = "sideways" }) end, "unknown mode")
end)

print(("%d passed, %d failed"):format(passed, failed))
os.exit(failed == 0 and 0 or 1)
