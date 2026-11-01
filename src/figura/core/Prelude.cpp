// FiguraBridge: the parts of Figura's Lua API that are pure logic, written in Lua (runs in the avatar's sandbox before
// the avatar scripts). Semantics follow Figura 0.1.6 EventsAPI/LuaEvent, PingAPI, ActionWheelAPI/Page/Action,
// KeybindAPI/FiguraKeybind, ConfigAPI, FiguraLuaPrinter, plus the global overrides from FiguraLuaRuntime
// (type, pairs, ipairs) and Figura's math.lua. Native hooks come in through the temporary global __fb.
#include "figura/core/Avatar.h"

namespace bf::figura {

char const* figuraPreludeSource() {
    return R"LUA(
local fb = __fb
__fb = nil
local reg = fb.registry

-- ---------------------------------------------------------------- type / pairs / ipairs (FiguraLuaRuntime)
local rawtype = type
type = function(v)
  local t = rawtype(v)
  if t == "userdata" or t == "table" then
    local mt = getmetatable(v)
    if rawtype(mt) == "table" then
      local tn = rawget(mt, "__type")
      if tn ~= nil then return tn end
    end
  end
  return t
end
local gpairs, gipairs = pairs, ipairs
pairs = function(t, ...)
  local mt = getmetatable(t)
  if rawtype(mt) == "table" and rawtype(rawget(mt, "__pairs")) == "function" then return rawget(mt, "__pairs")(t, ...) end
  return gpairs(t, ...)
end
ipairs = function(t, ...)
  local mt = getmetatable(t)
  if rawtype(mt) == "table" and rawtype(rawget(mt, "__ipairs")) == "function" then return rawget(mt, "__ipairs")(t, ...) end
  return gipairs(t, ...)
end

-- ---------------------------------------------------------------- printing (FiguraLuaPrinter)
local function tostr(v) return tostring(v) end
print = function(...)
  local n = select("#", ...)
  local parts = {}
  for i = 1, n do parts[i] = tostr((select(i, ...))) end
  fb.print(table.concat(parts, "\t"))
  return ...
end
log = print
local function dump(v, depth, indent, seen)
  if rawtype(v) ~= "table" or depth <= 0 then return tostr(v) end
  if seen[v] then return "<recursive>" end
  seen[v] = true
  local out = {"{"}
  for k, x in gpairs(v) do
    out[#out + 1] = indent .. "  [" .. tostr(k) .. "] = " .. dump(x, depth - 1, indent .. "  ", seen)
  end
  out[#out + 1] = indent .. "}"
  seen[v] = nil
  return table.concat(out, "\n")
end
printTable = function(t, depth, silent)
  local s = dump(t, depth or 1, "", {})
  if not silent then fb.print(s) end
  return s
end
logTable = printTable
printJson = function(...) return print(...) end
logJson = printJson

-- ---------------------------------------------------------------- events (EventsAPI + LuaEvent)
local MAX_FUNCTIONS = 1024
local EventMT = {__type = "Event"}
EventMT.__index = {}
local Event = EventMT.__index
local function newEvent(piped)
  return setmetatable({fns = {}, queue = {}, removal = {}, names = {}, piped = piped}, EventMT)
end
local function flush(e)
  for _, f in gipairs(e.removal) do
    for i, g in gipairs(e.fns) do if g == f then table.remove(e.fns, i) break end end
  end
  e.removal = {}
  for _, f in gipairs(e.queue) do e.fns[#e.fns + 1] = f end
  e.queue = {}
end
function Event.register(self, func, name)
  if rawtype(func) ~= "function" then error("bad argument #1 to 'register' (function expected)", 2) end
  if #self.fns + #self.queue >= MAX_FUNCTIONS then error("Reached maximum limit of " .. MAX_FUNCTIONS .. " functions in one event!", 2) end
  self.queue[#self.queue + 1] = func
  if name ~= nil then
    name = tostring(name)
    self.names[name] = self.names[name] or {}
    self.names[name][#self.names[name] + 1] = func
  end
  return self
end
function Event.clear(self) self.fns, self.queue, self.removal, self.names = {}, {}, {}, {} end
function Event.remove(self, what)
  flush(self)
  if rawtype(what) == "function" then
    self.removal[#self.removal + 1] = what
    for _, list in gpairs(self.names) do
      for i, f in gipairs(list) do if f == what then table.remove(list, i) break end end
    end
    return 1
  elseif rawtype(what) == "string" then
    local list = self.names[what] or {}
    self.names[what] = nil
    for _, f in gipairs(list) do self.removal[#self.removal + 1] = f end
    return #list
  end
  error("Invalid type to remove", 2)
end
function Event.getRegisteredCount(self, name) return #(self.names[name] or {}) end
EventMT.__len = function(self) return #self.fns + #self.queue end
EventMT.__tostring = function() return "Event" end
local function callEvent(e, ...)
  flush(e)
  if e.piped then
    local args = table.pack(...)
    for _, f in gipairs(e.fns) do args = table.pack(f(table.unpack(args, 1, args.n))) end
    return table.unpack(args, 1, args.n)
  end
  local result = {}
  for _, f in gipairs(e.fns) do
    local r = table.pack(f(...))
    for i = 1, r.n do table.insert(result, 1, r[i]) end
  end
  return table.unpack(result)
end
local eventNames = {"ENTITY_INIT", "TICK", "WORLD_TICK", "RENDER", "POST_RENDER", "WORLD_RENDER", "POST_WORLD_RENDER",
  "SKULL_RENDER", "CHAT_SEND_MESSAGE", "CHAT_RECEIVE_MESSAGE", "MOUSE_SCROLL", "MOUSE_MOVE", "MOUSE_PRESS", "KEY_PRESS",
  "CHAR_TYPED", "USE_ITEM", "ARROW_RENDER", "TRIDENT_RENDER", "ITEM_RENDER", "ON_PLAY_SOUND", "RESOURCE_RELOAD",
  "DAMAGE", "TOTEM"}
local piped = {CHAT_SEND_MESSAGE = true}
local evs = {}
for _, n in gipairs(eventNames) do evs[n] = newEvent(piped[n]) end
local EventsMT = {__type = "EventsAPI"}
EventsMT.__index = function(_, k)
  if rawtype(k) ~= "string" then return nil end
  if k == "getEvents" then return function() local t = {} for n, e in gpairs(evs) do t[n] = e end return t end end
  return evs[string.upper(k)]
end
EventsMT.__newindex = function(_, k, f)
  local e = evs[string.upper(tostring(k))]
  if e == nil then error("Cannot assign value on key \"" .. tostring(k) .. "\"", 2) end
  Event.register(e, f, nil)
end
EventsMT.__tostring = function() return "EventsAPI" end
events = setmetatable({}, EventsMT)
figuraMetatables.EventsAPI = EventsMT
figuraMetatables.Event = EventMT
reg.callEvent(function(name, ...) local e = evs[name] if e then return callEvent(e, ...) end end)

-- ---------------------------------------------------------------- pings (PingAPI): host runs them locally
local pingFns = {}
local PingMT = {__type = "PingAPI"}
PingMT.__index = function(_, k)
  local f = pingFns[k]
  if f == nil then return nil end
  return function(...) return f(...) end
end
PingMT.__newindex = function(_, k, f)
  if rawtype(f) ~= "function" then error("Pings must be functions", 2) end
  pingFns[k] = f
end
PingMT.__tostring = function() return "PingAPI" end
pings = setmetatable({}, PingMT)
figuraMetatables.PingAPI = PingMT

-- ---------------------------------------------------------------- keybinds (KeybindAPI / FiguraKeybind)
local KeyMT = {__type = "Keybind"}
KeyMT.__index = {}
local Key = KeyMT.__index
local keys = {}
function Key.isPressed(self) return fb.keyDown(self.id) end
function Key.setKey(self, k) self.key = k fb.keySetKey(self.id, k) return self end
function Key.getKey(self) return self.key end
function Key.getKeyName(self) return self.key end
function Key.getID(self) return self.id end
function Key.getName(self) return self.name end
function Key.setName(self, n) self.name = n return self end
function Key.isDefault(self) return self.key == self.default end
function Key.setEnabled(self, b) self.enabled = b fb.keyEnable(self.id, b) return self end
function Key.isEnabled(self) return self.enabled end
function Key.setGUI(self, b) self.gui = b return self end
function Key.isGUI(self) return self.gui end
function Key.setOnPress(self, f) self.press = f return self end
function Key.setOnRelease(self, f) self.release = f return self end
function Key.onPress(self, f) return Key.setOnPress(self, f) end
function Key.onRelease(self, f) return Key.setOnRelease(self, f) end
KeyMT.__newindex = function(self, k, v)
  if k == "press" or k == "release" then rawset(self, k, v) else error("Cannot assign value on key \"" .. tostring(k) .. "\"", 2) end
end
KeyMT.__tostring = function(self) return tostring(self.name) .. " (Keybind)" end
local KeyAPIMT = {__type = "KeybindAPI"}
KeyAPIMT.__index = {}
function KeyAPIMT.__index.newKeybind(_, name, key, gui)
  key = key or "key.keyboard.unknown"
  local id = fb.keyNew(tostring(name), key, gui and true or false)
  local o = {id = id, name = name, key = key, default = key, enabled = true, gui = gui and true or false}
  rawset(o, "press", nil)
  local k = setmetatable(o, KeyMT)
  keys[id] = k
  fb.keyRef(id, k)
  return k
end
KeyAPIMT.__index.of = KeyAPIMT.__index.newKeybind
function KeyAPIMT.__index.getVanillaKey(_, id) return fb.keyVanilla(id) end
function KeyAPIMT.__index.getKeybinds(_) local t = {} for _, k in gpairs(keys) do t[k.name] = k end return t end
function KeyAPIMT.__index.fromVanilla(self, id) return KeyAPIMT.__index.newKeybind(self, id, fb.keyVanilla(id)) end
KeyAPIMT.__tostring = function() return "KeybindAPI" end
keybinds = setmetatable({}, KeyAPIMT)
figuraMetatables.KeybindAPI = KeyAPIMT
figuraMetatables.Keybind = KeyMT
reg.keyEdge(function(k, down)
  local f = down and rawget(k, "press") or rawget(k, "release")
  if rawtype(f) == "function" then f(k) end
end)

-- ---------------------------------------------------------------- action_wheel (ActionWheelAPI / Page / Action)
local ActionMT = {__type = "Action"}
ActionMT.__index = {}
local Action = ActionMT.__index
local function D(self) return rawget(self, "_d") end
local function chainSetter(field) return function(self, v) D(self)[field] = v return self end end
for _, f in gipairs({"title", "toggleTitle", "color", "hoverColor", "toggleColor", "item", "hoverItem", "toggleItem",
                     "texture", "hoverTexture", "toggleTexture", "leftClick", "rightClick", "toggle", "untoggle",
                     "scroll"}) do
  local cap = f:sub(1, 1):upper() .. f:sub(2)
  Action["set" .. cap] = chainSetter(f)
  Action[f] = chainSetter(f)
  Action["get" .. cap] = function(self) return D(self)[f] end
end
Action.setOnLeftClick, Action.onLeftClick = chainSetter("leftClick"), chainSetter("leftClick")
Action.setOnRightClick, Action.onRightClick = chainSetter("rightClick"), chainSetter("rightClick")
Action.setOnToggle, Action.onToggle = chainSetter("toggle"), chainSetter("toggle")
Action.setOnUntoggle, Action.onUntoggle = chainSetter("untoggle"), chainSetter("untoggle")
Action.setOnScroll, Action.onScroll = chainSetter("scroll"), chainSetter("scroll")
function Action.setToggled(self, b) D(self).toggled = b and true or false return self end
function Action.toggled(self, b) return Action.setToggled(self, b) end
function Action.isToggled(self) return D(self).toggled == true end
local actionFieldAlias = {leftClick = "leftClick", rightClick = "rightClick", toggle = "toggle", untoggle = "untoggle", scroll = "scroll"}
ActionMT.__newindex = function(self, k, v)
  if actionFieldAlias[k] then D(self)[k] = v else error("Cannot assign value on key \"" .. tostring(k) .. "\"", 2) end
end
ActionMT.__tostring = function(self) return "Action" end
local function newActionObj() return setmetatable({_d = {}}, ActionMT) end
local PageMT = {__type = "Page"}
PageMT.__index = {}
local Page = PageMT.__index
function Page.newAction(self, index)
  local a = newActionObj()
  if index then table.insert(self.actions, index, a) else self.actions[#self.actions + 1] = a end
  return a
end
function Page.getAction(self, i) return self.actions[i] end
function Page.setAction(self, i, a)
  if i == -1 then i = #self.actions + 1 end
  self.actions[i] = a
  return self
end
function Page.getActions(self, slot) if slot then return self.actions[slot] end local t = {} for i, a in gpairs(self.actions) do t[i] = a end return t end
function Page.getTitle(self) return self.title end
function Page.setKeepSlots(self, b) self.keepSlots = b return self end
function Page.shouldKeepSlots(self) return self.keepSlots end
function Page.setSlotsShift(self, n) self.shift = n return self end
function Page.getSlotsShift(self) return self.shift or 1 end
PageMT.__tostring = function(self) return tostring(self.title) .. " (Page)" end
local wheel = {pages = {}, current = nil, enabled = false, selected = 0}
local AWMT = {__type = "ActionWheelAPI"}
AWMT.__index = {}
local AW = AWMT.__index
function AW.newPage(_, title)
  local p = setmetatable({title = title, actions = {}}, PageMT)
  if title then wheel.pages[title] = p end
  return p
end
function AW.setPage(_, p)
  if rawtype(p) == "string" then p = wheel.pages[p] end
  wheel.current = p
  return action_wheel
end
function AW.getPage(_, title) if title then return wheel.pages[title] end return wheel.pages end
function AW.getCurrentPage(_) return wheel.current end
function AW.isEnabled(_) return wheel.enabled end
function AW.getSelected(_) return wheel.selected end
function AW.getSelectedAction(_) return wheel.current and wheel.current.actions[wheel.selected] end
function AW.execute(_, index, right)
  local p = wheel.current
  if not p then return end
  local act = p.actions[index or wheel.selected]
  if not act then return end
  local a = D(act)
  if right then
    if a.rightClick then a.rightClick(act) end
  else
    if a.toggle or a.untoggle then
      a.toggled = not a.toggled
      if a.toggled and a.toggle then a.toggle(true, act) elseif not a.toggled then
        if a.untoggle then a.untoggle(false, act) elseif a.toggle then a.toggle(false, act) end
      end
    end
    if a.leftClick then a.leftClick(act) end
  end
end
function AW.newAction(_) return newActionObj() end
AWMT.__newindex = function(_, k, v)
  if k == "leftClick" or k == "rightClick" or k == "scroll" then wheel[k] = v
  else error("Cannot assign value on key \"" .. tostring(k) .. "\"", 2) end
end
AWMT.__tostring = function() return "ActionWheelAPI" end
action_wheel = setmetatable({}, AWMT)
figuraMetatables.ActionWheelAPI = AWMT
figuraMetatables.Page = PageMT
figuraMetatables.Action = ActionMT
-- native side reads the wheel to draw it in the Bedrock Forge menu
reg.wheel(function()
  local p = wheel.current
  if not p then return nil end
  local out = {title = p.title}
  for i, act in gpairs(p.actions) do
    local a = D(act)
    local title = a.toggled and a.toggleTitle or a.title
    out[i] = {title = title and tostring(title) or ("Action " .. i), toggled = a.toggled == true,
              color = a.toggled and a.toggleColor or a.color, item = a.item}
  end
  return out
end)

-- ---------------------------------------------------------------- config (ConfigAPI, in-memory + native store)
local cfgName = "default"
local CfgMT = {__type = "ConfigAPI"}
CfgMT.__index = {}
function CfgMT.__index.setName(_, n) cfgName = tostring(n) return config end
function CfgMT.__index.getName(_) return cfgName end
function CfgMT.__index.save(_, k, v) fb.cfgSave(cfgName, k, v) return config end
function CfgMT.__index.load(_, k) return fb.cfgLoad(cfgName, k) end
config = setmetatable({}, CfgMT)
figuraMetatables.ConfigAPI = CfgMT

-- ---------------------------------------------------------------- json (minimal FiguraLuaJson)
toJson = function(v) return fb.toJson(v) end
parseJson = function(s) return fb.parseJson(s) end

-- ---------------------------------------------------------------- Figura math.lua (verbatim)
function math.clamp(val, min, max)
   return min > max and max or val > max and max or val < min and min or val
end
function math.lerp(a, b, t)
  return a + (b - a) * t
end
function math.round(arg)
  return math.floor(arg + 0.5)
end
function math.map(value, min1, max1, min2, max2)
  return (value - min1) / (max1 - min1) * (max2 - min2) + min2
end
function math.shortAngle(a, b)
    local x = (a - b) % 360
    return x - ((2 * x) % 360)
end
function math.lerpAngle(a, b, t)
  return (a + math.shortAngle(a, b) * t) % 360
end
function math.sign(x)
  return x == 0 and 0 or x > 0 and 1 or -1
end
vec = vectors.vec
math.playerScale = 0.9375
math.worldScale = 1.0666666667
)LUA";
}

} // namespace bf::figura
