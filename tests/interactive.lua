-- Driven by tests/pty_test.py inside a pseudo-terminal.
--   lua tests/interactive.lua "<cpath template>" <result file> [fail]
package.cpath = arg[1] .. ";" .. package.cpath
local ui = require("ftxui")
local result, mode = arg[2], arg[3]

local input = ui.input{ placeholder = "type here" }
local clicks, ticks = 0, 0
local button = ui.button{
  label = "Add",
  on_click = function()
    if mode == "fail" then error("boom from interactive click") end
    clicks = clicks + 1
  end,
}
local body = ui.renderer(ui.vertical{ input, button }, function()
  return ui.vbox{
    ui.text("clicks: " .. clicks),
    input:render():border(),
    button:render(),
  }
end)
local root = ui.catch_event(body, function(ev)
  if ev.character == "~" then ui.exit(); return true end
end)

ui.run(root, { tick = 0.05, on_tick = function() ticks = ticks + 1 end })

local f = assert(io.open(result, "w"))
f:write(("value=%s clicks=%d ticked=%s\n"):format(input.value, clicks, tostring(ticks > 0)))
f:close()
