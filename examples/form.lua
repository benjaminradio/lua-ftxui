-- An IUP-style form: widgets are declared once, a renderer lays them out.
--   lua examples/form.lua        (Esc or the Quit button exits)
package.cpath = "./?.so;./?.dll;" .. package.cpath
local ui = require("ftxui")

local name = ui.input{ placeholder = "Your name" }
local secret = ui.input{ placeholder = "Password", password = true }
local news = ui.checkbox{ label = "Subscribe to the newsletter" }
local size = ui.radiobox{ entries = { "Small", "Medium", "Large" }, selected = 2 }
local volume = ui.slider{ label = "Volume", min = 0, max = 100, value = 30, step = 5 }
local flavour = ui.dropdown{ entries = { "Vanilla", "Chocolate", "Strawberry", "Mint" } }

local status = "Fill in the form and press Submit."
local submit = ui.button{
  label = "Submit",
  on_click = function()
    status = ("Hello %s! size=%s volume=%g flavour=%s news=%s"):format(
      name.value == "" and "stranger" or name.value,
      size.entries[size.selected], volume.value,
      flavour.entries[flavour.selected], tostring(news.checked))
  end,
}
local quit = ui.button{ label = "Quit", on_click = ui.exit }

local form = ui.vertical{ name, secret, news, size, volume, flavour, ui.horizontal{ submit, quit } }

local root = ui.renderer(form, function()
  return ui.vbox{
    ui.text{ "Registration", bold = true, hcenter = true },
    ui.separator(),
    ui.hbox{ ui.text("Name     "), name:render() },
    ui.hbox{ ui.text("Password "), secret:render() },
    news:render(),
    ui.hbox{ ui.text("Size  "), size:render() },
    volume:render(),
    ui.hbox{ ui.text("Flavour "), flavour:render() },
    ui.separator(),
    ui.hbox{ submit:render(), quit:render() },
    ui.paragraph(status):color("gray"),
    border = "rounded", width = 60,
  }
end)

root = ui.catch_event(root, function(ev)
  if ev.key == "Escape" then ui.exit(); return true end
end)

ui.run(root, { mode = "fit" })
