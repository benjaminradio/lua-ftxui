-- Static rendering: build an element tree and print it. No terminal needed.
--   lua examples/hello.lua
package.cpath = "./?.so;./?.dll;" .. package.cpath
local ui = require("ftxui")

ui.color_support("256")

local card = ui.vbox{
  ui.text{ "lua-ftxui", bold = true, color = "cyan", hcenter = true },
  ui.separator(),
  ui.hbox{
    ui.text("CPU "), ui.gauge(0.42, { color = "green" }):width(20), ui.text(" 42%"),
  },
  ui.hbox{
    ui.text("MEM "), ui.gauge(0.77, { color = "yellow" }):width(20), ui.text(" 77%"),
  },
  ui.table{
    { "host", "status" },
    { "alpha", ui.text{ "up", color = "green" } },
    { "beta", ui.text{ "down", color = "red" } },
    header = true,
  },
  title = "status",
  border = "rounded",
}

io.write(ui.render(card, { color = true }), "\n")
