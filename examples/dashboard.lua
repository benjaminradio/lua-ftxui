-- Live-updating view driven by a timer: spinner, gauges, graph and a table.
--   lua examples/dashboard.lua   (q exits)
package.cpath = "./?.so;./?.dll;" .. package.cpath
local ui = require("ftxui")

local frame = 0
local function load(phase) return 0.5 + 0.45 * math.sin(frame / 10 + phase) end

local root = ui.renderer(function()
  local rows = { { "service", "load", "state" }, header = true }
  for i, svc in ipairs{ "api", "db", "cache", "queue" } do
    local l = load(i)
    rows[#rows + 1] = {
      svc,
      ("%3d%%"):format(math.floor(l * 100)),
      ui.text{ l > 0.8 and "busy" or "ok", color = l > 0.8 and "red" or "green" },
    }
  end
  return ui.vbox{
    ui.hbox{ ui.spinner(frame, 15), ui.text(" monitoring "), ui.filler(), ui.text("q to quit"):dim() },
    ui.separator(),
    ui.hbox{ ui.text("api   "), ui.gauge(load(1), { color = "green" }) },
    ui.hbox{ ui.text("db    "), ui.gauge(load(2), { color = "yellow" }) },
    ui.table(rows),
    ui.graph(function(w, h)
      local out = {}
      for x = 1, w do out[x] = math.floor((0.5 + 0.5 * math.sin((x + frame) / 8)) * (h - 1)) end
      return out
    end):height(6):color("cyan"),
    border = "rounded", title = "dashboard",
  }
end)

root = ui.catch_event(root, function(ev)
  if ev.character == "q" then ui.exit(); return true end
end)

ui.run(root, { tick = 0.1, on_tick = function() frame = frame + 1 end })
