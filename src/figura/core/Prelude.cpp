// FiguraBridge: the parts of Figura's Lua API that are pure logic, written in Lua (runs in the avatar's sandbox before
// the avatar scripts). Semantics follow Figura 0.1.6 EventsAPI/LuaEvent, PingAPI/PingFunction/PingArg,
// ActionWheelAPI/Page/Action (+ gui ActionWheel.execute), KeybindAPI/FiguraKeybind, ConfigAPI, FiguraLuaPrinter,
// FiguraLuaJson, plus the global overrides from FiguraLuaRuntime (type, pairs, ipairs) and Figura's math.lua.
// Figura objects are zero-size userdata (fb.udata) whose state lives in weak tables, so scripts see userdata like on
// Java (no rawset / pairs into internals). Native hooks come in through the temporary global __fb.
#include "figura/core/Avatar.h"

namespace bf::figura {

char const* figuraPreludeSource() {
    return R"LUA(
local fb = __fb
__fb = nil
local reg = fb.registry
local rawtype, rawmt, udata = type, fb.rawmt, fb.udata
local gpairs, gipairs, gnext, gselect, gerror, gtostring = pairs, ipairs, next, select, error, tostring
local tconcat, tpack, tunpack, sformat, supper = table.concat, table.pack, table.unpack, string.format, string.upper
local mfloor = math.floor
local vecs = vectors
local mats = matrices

-- ---------------------------------------------------------------- type / pairs / ipairs (FiguraLuaRuntime)
type = function(v)
  local t = rawtype(v)
  if t == "userdata" then
    local mt = rawmt(v)
    if rawtype(mt) == "table" then
      local tn = rawget(mt, "__type")
      if rawtype(tn) == "string" then return tn end
    end
    return "userdata"
  elseif t == "table" then
    local mt = rawmt(v)
    if rawtype(mt) == "table" then
      local tn = rawget(mt, "__type")
      if tn ~= nil then return tn end
    end
  end
  return t
end
local ftype = type
pairs = function(t, ...)
  local rt = rawtype(t)
  if rt == "table" or rt == "userdata" then
    local mt = rawmt(t)
    if rawtype(mt) == "table" and rawtype(rawget(mt, "__pairs")) == "function" then return rawget(mt, "__pairs")(t, ...) end
  end
  return gpairs(t, ...)
end
ipairs = function(t, ...)
  local rt = rawtype(t)
  if rt == "table" or rt == "userdata" then
    local mt = rawmt(t)
    if rawtype(mt) == "table" and rawtype(rawget(mt, "__ipairs")) == "function" then return rawget(mt, "__ipairs")(t, ...) end
  end
  return gipairs(t, ...)
end

-- ---------------------------------------------------------------- helpers: Figura objects + LuaTypeManager argument checks
local function argErr(i, fn, expected, v, lvl)
  gerror(sformat("Invalid argument %d to function %s. Expected %s, but got %s", i, fn, expected, ftype(v)), (lvl or 2) + 1)
end
local function optFn(v, i, fn) if v ~= nil and rawtype(v) ~= "function" then argErr(i, fn, "Function", v, 2) end return v end
local function optStr(v, i, fn)
  if v == nil then return nil end
  local t = rawtype(v)
  if t == "string" then return v elseif t == "number" then return gtostring(v) end
  argErr(i, fn, "String", v, 2)
end
local function optInt(v, i, fn, def)
  if v == nil then return def end
  local n = tonumber(v)
  if n == nil then argErr(i, fn, "Integer", v, 2) end
  if n ~= n then return 0 end
  if n >= 2147483647 then return 2147483647 elseif n <= -2147483648 then return -2147483648 end
  return n >= 0 and mfloor(n) or -mfloor(-n)
end
local function bool(v) return v ~= nil and v ~= false end

-- define a Figura class: methods (incl. exposed metamethod names), optional __index/__newindex fallbacks
local function class(name, methods, opts)
  opts = opts or {}
  local mt = {__type = name, __fbm = methods, __metatable = nil}
  local index, newindex = opts.index, opts.newindex
  mt.__index = function(self, k)
    local m = methods[k]
    if m ~= nil then return m end
    if index then return index(self, k) end
    return nil
  end
  mt.__newindex = function(self, k, v)
    if newindex then return newindex(self, k, v) end
    gerror('Cannot assign value on key "' .. gtostring(k) .. '"', 2)
  end
  mt.__tostring = opts.tostring or function() return name end
  for k, v in gpairs(opts.meta or {}) do mt[k] = v end
  if index and methods.__index == nil then methods.__index = function(self, k) return index(self, k) end end
  if newindex and methods.__newindex == nil then methods.__newindex = function(self, k, v) return newindex(self, k, v) end end
  figuraMetatables[name] = mt
  return mt
end
local function weak() return setmetatable({}, {__mode = "k"}) end

-- ---------------------------------------------------------------- printing (FiguraLuaPrinter)
local numfmt = fb.numfmt
local function isFigObj(v)
  local rt = rawtype(v)
  if rt == "userdata" then return true end
  return false
end
local function printText(v, quote)
  local t = rawtype(v)
  if t == "number" then return numfmt(v) end
  local s = gtostring(v)
  if t == "string" and quote then s = '"' .. s .. '"' end
  return s
end
local tableToText
local function methodNames(v)
  local mt = rawmt(v)
  local names = {}
  if rawtype(mt) == "table" and rawtype(rawget(mt, "__fbm")) == "table" then
    for k in gpairs(rawget(mt, "__fbm")) do names[#names + 1] = k end
  else
    names = fb.methodNames(v)
  end
  table.sort(names, function(a, b) return gtostring(a) < gtostring(b) end)
  return names
end
local function entry(spacing, k, v, depth, indent)
  local s = spacing .. "\t[" .. printText(k, true) .. "] = "
  local t = rawtype(v)
  if t == "table" or t == "userdata" then s = s .. tableToText(v, depth - 1, indent + 1)
  else s = s .. printText(v, true) end
  return s .. "\n"
end
local function userdataToText(v, depth, indent)
  if depth <= 0 then return printText(v, true) end
  local spacing = string.rep("\t", indent - 1)
  local out = {"userdata: {\n"}
  for _, n in gipairs(methodNames(v)) do
    if rawtype(n) == "string" and n:sub(1, 2) ~= "__" then
      out[#out + 1] = spacing .. "\t[" .. printText(n, true) .. "] = function: " .. n .. "\n"
    end
  end
  out[#out + 1] = spacing .. "}"
  return tconcat(out)
end
tableToText = function(v, depth, indent)
  if rawtype(v) == "userdata" then return userdataToText(v, depth, indent) end
  if rawtype(v) ~= "table" or depth <= 0 then return printText(v, true) end
  local spacing = string.rep("\t", indent - 1)
  local out = {"table: {\n"}
  for k, x in gnext, v do out[#out + 1] = entry(spacing, k, x, depth, indent) end
  out[#out + 1] = spacing .. "}"
  return tconcat(out)
end
print = function(...)
  local n = gselect("#", ...)
  local parts = {}
  for i = 1, n do parts[i] = printText((gselect(i, ...)), false) .. "\t" end
  local text = tconcat(parts)
  fb.print(text)
  return text
end
log = print
printTable = function(...)
  local n = gselect("#", ...)
  local text, silent = "", false
  if n > 0 then
    local t, depth, s = ...
    depth = rawtype(depth) == "number" and optInt(depth, 2, "printTable", 1) or 1
    text = tableToText(t, depth, 1)
    silent = s == true
  end
  if not silent then fb.print(text) end
  return text
end
logTable = printTable
-- TextUtils.tryParseJson + Component.getString
local parseJsonRaw = fb.parseJson
local function componentText(c)
  local t = rawtype(c)
  if t == "string" then return c
  elseif t == "number" then return numfmt(c)
  elseif t == "boolean" then return gtostring(c)
  elseif t == "table" then
    if #c > 0 then
      local s = {}
      for i = 1, #c do s[#s + 1] = componentText(c[i]) end
      return tconcat(s)
    end
    local s = ""
    if c.text ~= nil then s = gtostring(c.text)
    elseif c.translate ~= nil then s = gtostring(c.translate)
    elseif c.keybind ~= nil then s = gtostring(c.keybind)
    elseif c.selector ~= nil then s = gtostring(c.selector)
    elseif rawtype(c.score) == "table" then s = gtostring(c.score.value or "") end
    if rawtype(c.extra) == "table" then for i = 1, #c.extra do s = s .. componentText(c.extra[i]) end end
    return s
  end
  return ""
end
printJson = function(...)
  local n = gselect("#", ...)
  local parts = {}
  for i = 1, n do
    local s = gtostring((gselect(i, ...)))
    local ok, c = pcall(parseJsonRaw, s)
    parts[i] = (ok and c ~= nil) and componentText(c) or s
  end
  local text = tconcat(parts)
  fb.print(text)
  return text
end
logJson = printJson

-- ---------------------------------------------------------------- json (FiguraLuaJson)
local nativeToJson = fb.toJson
toJson = function(...)
  if gselect("#", ...) == 0 then return nil end
  return nativeToJson((...))
end
parseJson = function(s)
  if rawtype(s) ~= "string" and rawtype(s) ~= "number" then argErr(1, "parseJson", "String", s) end
  return parseJsonRaw(gtostring(s))
end

-- ---------------------------------------------------------------- events (EventsAPI + LuaEvent)
local MAX_FUNCTIONS = 1024
local ES = weak()
local EventM = {}
local function flush(s)
  for _, f in gipairs(s.removal) do
    for i, g in gipairs(s.fns) do if g == f then table.remove(s.fns, i) break end end
  end
  s.removal = {}
  for _, f in gipairs(s.queue) do s.fns[#s.fns + 1] = f end
  s.queue = {}
end
local function evLen(self) local s = ES[self] return #s.fns + #s.queue end
function EventM.register(self, func, name)
  local s = ES[self]
  if rawtype(func) ~= "function" then argErr(2, "register", "Function", func) end
  name = optStr(name, 3, "register")
  if evLen(self) >= MAX_FUNCTIONS then gerror("Reached maximum limit of " .. MAX_FUNCTIONS .. " functions in one event!", 2) end
  s.queue[#s.queue + 1] = func
  if name ~= nil then
    local set = s.names[name]
    if not set then set = {} s.names[name] = set end
    local dup = false
    for _, f in gipairs(set) do if f == func then dup = true break end end
    if not dup then set[#set + 1] = func end
  end
  return self
end
function EventM.clear(self)
  local s = ES[self]
  s.fns, s.queue, s.removal, s.names = {}, {}, {}, {}
end
function EventM.remove(self, what)
  local s = ES[self]
  flush(s)
  local t = rawtype(what)
  if t == "function" then
    s.removal[#s.removal + 1] = what
    for n, set in gpairs(s.names) do
      local done = false
      for i, f in gipairs(set) do if f == what then table.remove(set, i) done = true break end end
      if #set == 0 then s.names[n] = nil end
      if done then break end
    end
    return 1
  elseif t == "string" or t == "number" then
    local set = s.names[gtostring(what)] or {}
    s.names[gtostring(what)] = nil
    for _, f in gipairs(set) do s.removal[#s.removal + 1] = f end
    return #set
  end
  gerror("Illegal argument to remove(): " .. (what == nil and "null" or ftype(what)), 2)
end
function EventM.getRegisteredCount(self, name)
  local set = ES[self].names[optStr(name, 2, "getRegisteredCount")]
  return set and #set or 0
end
EventM.__len = evLen
local EventMT = class("Event", EventM, {meta = {__len = evLen}})
local function newEvent(piped)
  local e = udata(EventMT)
  ES[e] = {fns = {}, queue = {}, removal = {}, names = {}, piped = piped}
  return e
end
local function callEvent(e, ...)
  local s = ES[e]
  flush(s)
  if s.piped then
    local args = tpack(...)
    local i = 1
    while s.fns[i] do args = tpack(s.fns[i](tunpack(args, 1, args.n))) i = i + 1 end
    return tunpack(args, 1, args.n)
  end
  local result, n = {}, 0
  local i = 1
  while s.fns[i] do
    local r = tpack(s.fns[i](...))
    for k = 1, r.n do if r[k] ~= nil then n = n + 1 result[n] = r[k] end end
    i = i + 1
  end
  return tunpack(result, 1, n)
end
local eventNames = {"ENTITY_INIT", "TICK", "WORLD_TICK", "RENDER", "POST_RENDER", "WORLD_RENDER", "POST_WORLD_RENDER",
  "CHAT_SEND_MESSAGE", "CHAT_RECEIVE_MESSAGE", "SKULL_RENDER", "MOUSE_SCROLL", "MOUSE_MOVE", "MOUSE_PRESS", "KEY_PRESS",
  "CHAR_TYPED", "USE_ITEM", "ARROW_RENDER", "TRIDENT_RENDER", "ITEM_RENDER", "ON_PLAY_SOUND", "RESOURCE_RELOAD",
  "TOTEM", "DAMAGE"}
local evs = {}
for _, n in gipairs(eventNames) do evs[n] = newEvent(n == "CHAT_SEND_MESSAGE") end
local EventsM = {}
function EventsM.getEvents(self) local t = {} for n, e in gpairs(evs) do t[n] = e end return t end
local function eventsIndex(self, k)
  if k == nil then return nil end
  local t = rawtype(k)
  if t ~= "string" and t ~= "number" then argErr(2, "__index", "String", k, 2) end
  return evs[supper(gtostring(k))]
end
local function eventsNewIndex(self, k, f)
  if rawtype(f) ~= "function" then argErr(3, "__newindex", "Function", f, 2) end
  local e = evs[supper(gtostring(k))]
  if e == nil then gerror('Cannot assign value on key "' .. gtostring(k) .. '"', 2) end
  EventM.register(e, f, nil)
end
local EventsMT = class("EventsAPI", EventsM, {index = eventsIndex, newindex = eventsNewIndex})
events = udata(EventsMT)

-- ---------------------------------------------------------------- pings (PingAPI / PingFunction / PingArg)
local pingFns, pingFuncs, pingQueue = {}, {}, {}
local function isVec(v) local t = ftype(v) return t == "Vector2" or t == "Vector3" or t == "Vector4" end
local function isMat(v) local t = ftype(v) return t == "Matrix2" or t == "Matrix3" or t == "Matrix4" end
-- write + read through PingArg: what survives the round trip, as fresh values
local function pingCopy(v, depth)
  if depth > 200 then gerror("Stack Overflow", 0) end
  local t = rawtype(v)
  if t == "boolean" then return v
  elseif t == "string" then return #v > 65535 and v:sub(1, 65535) or v
  elseif t == "number" then
    if v == mfloor(v) and v >= -2147483648 and v <= 2147483647 then return v end
    return v
  elseif t == "table" then
    local r = {}
    for k, x in gnext, v do
      local ck, cx = pingCopy(k, depth + 1), pingCopy(x, depth + 1)
      if ck ~= nil and ck == ck then r[ck] = cx end
    end
    return r
  elseif isVec(v) then return v:copy()
  elseif isMat(v) then
    local ok, c = pcall(function() return v:copy() end)
    return ok and c or nil
  end
  return nil
end
local function runPings()
  if #pingQueue == 0 then return end
  local q = pingQueue
  pingQueue = {}
  for _, p in gipairs(q) do
    local f = pingFns[p.name]
    if f then f(tunpack(p.args, 1, p.args.n)) end
  end
end
local function pingFunction(name)
  local pf = pingFuncs[name]
  if pf then return pf end
  pf = function(...)
    local n = gselect("#", ...)
    local args = {n = n}
    for i = 1, n do args[i] = pingCopy((gselect(i, ...)), 0) end
    pingQueue[#pingQueue + 1] = {name = name, args = args}
    return nil
  end
  pingFuncs[name] = pf
  return pf
end
local PingM = {}
local function pingKey(k, i, fn)
  local t = rawtype(k)
  if t == "string" then return k elseif t == "number" then return gtostring(k) end
  if k == nil then return nil end
  argErr(i, fn, "String", k, 3)
end
local function pingIndex(self, k)
  k = pingKey(k, 2, "__index")
  if k == nil or pingFns[k] == nil then return nil end
  return pingFunction(k)
end
local function pingNewIndex(self, k, f)
  k = pingKey(k, 2, "__newindex")
  if f ~= nil and rawtype(f) ~= "function" then argErr(3, "__newindex", "Function", f, 2) end
  if f == nil then pingFns[k] = nil pingFuncs[k] = nil
  else pingFns[k] = f pingFuncs[k] = nil end
end
local PingMT = class("PingAPI", PingM, {index = pingIndex, newindex = pingNewIndex})
pings = udata(PingMT)
-- the host can deliver a ping received from the server: fb.runPing(name, ...)
reg.runPing(function(name, ...)
  local f = pingFns[name]
  if f then return f(...) end
end)

-- ---------------------------------------------------------------- keybinds (KeybindAPI / FiguraKeybind)
local KS = weak()
local keyList = {}
-- InputConstants: name -> GLFW code, display name
local KEYS = {["key.keyboard.unknown"] = {-1, "Not Bound"}}
do
  for c = 65, 90 do KEYS["key.keyboard." .. string.char(c + 32)] = {c, string.char(c)} end
  for c = 48, 57 do KEYS["key.keyboard." .. string.char(c)] = {c, string.char(c)} end
  for i = 1, 25 do KEYS["key.keyboard.f" .. i] = {289 + i, "F" .. i} end
  for i = 0, 9 do KEYS["key.keyboard.keypad." .. i] = {320 + i, gtostring(i)} end
  local k = {
    {"keypad.decimal", 330, "."}, {"keypad.divide", 331, "/"}, {"keypad.multiply", 332, "*"},
    {"keypad.subtract", 333, "-"}, {"keypad.add", 334, "+"}, {"keypad.enter", 335, "Keypad Enter"},
    {"keypad.equal", 336, "="}, {"num.lock", 282, "Num Lock"}, {"down", 264, "Down Arrow"}, {"left", 263, "Left Arrow"},
    {"right", 262, "Right Arrow"}, {"up", 265, "Up Arrow"}, {"apostrophe", 39, "'"}, {"backslash", 92, "\\"},
    {"comma", 44, ","}, {"equal", 61, "="}, {"grave.accent", 96, "`"}, {"left.bracket", 91, "["}, {"minus", 45, "-"},
    {"period", 46, "."}, {"right.bracket", 93, "]"}, {"semicolon", 59, ";"}, {"slash", 47, "/"}, {"space", 32, "Space"},
    {"tab", 258, "Tab"}, {"left.alt", 342, "Left Alt"}, {"left.control", 341, "Left Control"},
    {"left.shift", 340, "Left Shift"}, {"left.win", 343, "Left Win"}, {"right.alt", 346, "Right Alt"},
    {"right.control", 345, "Right Control"}, {"right.shift", 344, "Right Shift"}, {"right.win", 347, "Right Win"},
    {"enter", 257, "Enter"}, {"escape", 256, "Escape"}, {"backspace", 259, "Backspace"}, {"delete", 261, "Delete"},
    {"end", 269, "End"}, {"home", 268, "Home"}, {"insert", 260, "Insert"}, {"page.down", 267, "Page Down"},
    {"page.up", 266, "Page Up"}, {"caps.lock", 280, "Caps Lock"}, {"pause", 284, "Pause"},
    {"scroll.lock", 281, "Scroll Lock"}, {"menu", 348, "Menu"}, {"print.screen", 283, "Print Screen"},
    {"world.1", 161, "World 1"}, {"world.2", 162, "World 2"}}
  for _, e in gipairs(k) do KEYS["key.keyboard." .. e[1]] = {e[2], e[3]} end
  KEYS["key.mouse.left"] = {0, "Left Button"}
  KEYS["key.mouse.right"] = {1, "Right Button"}
  KEYS["key.mouse.middle"] = {2, "Middle Button"}
  for i = 4, 8 do KEYS["key.mouse." .. i] = {i - 1, "Button " .. i} end
end
local function parseKey(key)
  if KEYS[key] then return key end
  for _, prefix in gipairs({"key.keyboard", "scancode", "key.mouse"}) do
    if key:sub(1, #prefix) == prefix then
      local num = tonumber(key:sub(#prefix + 2))
      if num and num == mfloor(num) then
        if prefix == "key.mouse" then num = num - 1 end
        for n, d in gpairs(KEYS) do
          if d[1] == num and ((prefix == "key.mouse") == (n:sub(1, 9) == "key.mouse")) and prefix ~= "scancode" then return n end
        end
        local nm = prefix .. "." .. gtostring(prefix == "key.mouse" and num + 1 or num)
        KEYS[nm] = {num, prefix == "key.mouse" and ("Button " .. (num + 1)) or nm}
        return nm
      end
      break
    end
  end
  gerror("Invalid key: " .. gtostring(key), 3)
end
-- vanilla KeyMappings: id -> {translated name, default key}
local VANILLA = {
  ["key.attack"] = {"Attack/Destroy", "key.mouse.left"}, ["key.use"] = {"Use Item/Place Block", "key.mouse.right"},
  ["key.forward"] = {"Walk Forwards", "key.keyboard.w"}, ["key.left"] = {"Strafe Left", "key.keyboard.a"},
  ["key.back"] = {"Walk Backwards", "key.keyboard.s"}, ["key.right"] = {"Strafe Right", "key.keyboard.d"},
  ["key.jump"] = {"Jump", "key.keyboard.space"}, ["key.sneak"] = {"Sneak", "key.keyboard.left.shift"},
  ["key.sprint"] = {"Sprint", "key.keyboard.left.control"}, ["key.drop"] = {"Drop Selected Item", "key.keyboard.q"},
  ["key.inventory"] = {"Open/Close Inventory", "key.keyboard.e"}, ["key.chat"] = {"Open Chat", "key.keyboard.t"},
  ["key.playerlist"] = {"List Players", "key.keyboard.tab"}, ["key.pickItem"] = {"Pick Block", "key.mouse.middle"},
  ["key.command"] = {"Open Command", "key.keyboard.slash"},
  ["key.socialInteractions"] = {"Social Interactions Screen", "key.keyboard.p"},
  ["key.screenshot"] = {"Take Screenshot", "key.keyboard.f2"},
  ["key.togglePerspective"] = {"Toggle Perspective", "key.keyboard.f5"},
  ["key.smoothCamera"] = {"Toggle Cinematic Camera", "key.keyboard.unknown"},
  ["key.fullscreen"] = {"Toggle Fullscreen", "key.keyboard.f11"},
  ["key.spectatorOutlines"] = {"Highlight Players (Spectators)", "key.keyboard.unknown"},
  ["key.swapOffhand"] = {"Swap Item With Offhand", "key.keyboard.f"},
  ["key.saveToolbarActivator"] = {"Save Hotbar Activator", "key.keyboard.c"},
  ["key.loadToolbarActivator"] = {"Load Hotbar Activator", "key.keyboard.x"},
  ["key.advancements"] = {"Advancements", "key.keyboard.l"},
  ["figura.config.action_wheel_button"] = {"Action Wheel Button", "key.keyboard.b"},
  ["figura.config.panic_button"] = {"Panic Button", "key.keyboard.unknown"},
}
for i = 1, 9 do VANILLA["key.hotbar." .. i] = {"Hotbar Slot " .. i, "key.keyboard." .. i} end
local function vanillaKey(id)
  local v = VANILLA[id]
  if not v then return nil end
  local h = fb.keyVanilla(id)
  if rawtype(h) == "string" and h ~= "" and h ~= "key.keyboard.unknown" and KEYS[h] then return h end
  return v[2]
end
local KeyM = {}
local function ks(self) return KS[self] end
function KeyM.setOnPress(self, f) ks(self).press = optFn(f, 2, "setOnPress") return self end
function KeyM.onPress(self, f) ks(self).press = optFn(f, 2, "onPress") return self end
function KeyM.setOnRelease(self, f) ks(self).release = optFn(f, 2, "setOnRelease") return self end
function KeyM.onRelease(self, f) ks(self).release = optFn(f, 2, "onRelease") return self end
function KeyM.setKey(self, k)
  local s = ks(self)
  s.key = parseKey(optStr(k, 2, "setKey") or "null")
  fb.keySetKey(s.id, s.key)
  return self
end
KeyM.key = KeyM.setKey
function KeyM.isDefault(self) local s = ks(self) return s.key == s.default end
function KeyM.getKey(self) return ks(self).key end
function KeyM.getKeyName(self) return KEYS[ks(self).key][2] end
function KeyM.getName(self) return ks(self).name end
function KeyM.getID(self) return KEYS[ks(self).key][1] end
function KeyM.isPressed(self) return ks(self).isDown == true end -- (gui or no screen open) and isDown
function KeyM.isEnabled(self) return ks(self).enabled end
function KeyM.setEnabled(self, b) local s = ks(self) s.enabled = bool(b) fb.keyEnable(s.id, s.enabled) return self end
KeyM.enabled = KeyM.setEnabled
function KeyM.isGuiEnabled(self) return ks(self).gui end
function KeyM.setGUI(self, b) local s = ks(self) s.gui = bool(b) fb.keyGui(s.id, s.gui) return self end
KeyM.gui = KeyM.setGUI
local function keyIndex(self, k)
  if k == "press" then return ks(self).press elseif k == "release" then return ks(self).release end
  return nil
end
local function keyNewIndex(self, k, v)
  if v ~= nil and rawtype(v) ~= "function" then argErr(3, "__newindex", "Function", v, 2) end
  if k == "press" then ks(self).press = v elseif k == "release" then ks(self).release = v
  else gerror('Cannot assign value on key "' .. gtostring(k) .. '"', 2) end
end
local KeyMT = class("Keybind", KeyM, {index = keyIndex, newindex = keyNewIndex,
  tostring = function(self) local s = ks(self) return gtostring(s.name) .. " (" .. s.key .. ") (Keybind)" end})
local KeyAPIM = {}
function KeyAPIM.newKeybind(self, name, key, gui)
  name = optStr(name, 2, "newKeybind")
  key = optStr(key, 3, "newKeybind") or "key.keyboard.unknown"
  key = parseKey(key)
  local k = udata(KeyMT)
  local id = fb.keyNew(name or "null", key, bool(gui))
  KS[k] = {id = id, name = name, key = key, default = key, enabled = true, gui = bool(gui), isDown = false, override = false}
  keyList[#keyList + 1] = k
  fb.keyRef(id, k)
  return k
end
KeyAPIM.of = KeyAPIM.newKeybind
function KeyAPIM.getVanillaKey(self, id) return vanillaKey(optStr(id, 2, "getVanillaKey")) end
function KeyAPIM.fromVanilla(self, id)
  id = optStr(id, 2, "fromVanilla")
  local v = VANILLA[id]
  if not v then gerror('Failed to find key: "' .. gtostring(id) .. '"', 2) end
  return KeyAPIM.newKeybind(self, "[Vanilla] " .. v[1], vanillaKey(id), false)
end
function KeyAPIM.getKeybinds(self)
  local t = {}
  for _, k in gipairs(keyList) do local s = KS[k] if s.name ~= nil then t[s.name] = k end end
  return t
end
local KeyAPIMT = class("KeybindAPI", KeyAPIM)
keybinds = udata(KeyAPIMT)
-- host: key state edges (Avatar::tick polls enabled keybinds); press/release(modifiers, keybind), true = override
reg.keyEdge(function(k, down)
  local s = KS[k]
  if not s then return end
  down = down and true or false
  local override = false
  if s.isDown ~= down then
    local f = down and s.press or s.release
    if f then
      local r = f(-1, k)
      override = r == true
    end
    s.override = override
  end
  s.isDown = down
  return s.override
end)

-- ---------------------------------------------------------------- action_wheel (ActionWheelAPI / Page / Action)
local AS = weak()   -- Action state
local PS = weak()   -- Page state
local vec3 = vecs.vec3
local function nullableVec3(fn, x, y, z)
  if x == nil then return nil end
  if ftype(x) == "Vector3" then return x:copy() end
  local nx = tonumber(x)
  if nx == nil then gerror("Illegal argument to " .. fn .. "(): " .. ftype(x), 3) end
  local ny = y == nil and 0 or tonumber(y)
  local nz = z == nil and 0 or tonumber(z)
  if ny == nil then argErr(3, fn, "Number", y, 3) end
  if nz == nil then argErr(4, fn, "Number", z, 3) end
  return vec3(nx, ny, nz)
end
local function itemArg(fn, v)
  if v == nil then return "minecraft:air" end -- ItemStack.EMPTY
  if rawtype(v) == "string" or ftype(v) == "ItemStack" then return v end
  gerror("Illegal argument to " .. fn .. "(): " .. gtostring(v), 3)
end
local function textureData(fn, tex, u, v, w, h, scale)
  if tex ~= nil and ftype(tex) ~= "Texture" then argErr(2, fn, "Texture", tex, 3) end
  u = u == nil and 0 or tonumber(u)
  v = v == nil and 0 or tonumber(v)
  if u == nil then argErr(3, fn, "Number", u, 3) end
  if v == nil then argErr(4, fn, "Number", v, 3) end
  w = optInt(w, 5, fn, nil)
  h = optInt(h, 6, fn, nil)
  if (w == nil or h == nil) and tex == nil then gerror("Cannot invoke \"FiguraTexture.getWidth()\" because \"texture\" is null", 3) end
  local d = tex and tex:getDimensions()
  return {texture = tex, u = u, v = v, width = w or mfloor(d.x), height = h or mfloor(d.y), scale = scale == nil and 1 or tonumber(scale)}
end
local ActionM = {}
local function as(self) return AS[self] end
local function setter(field, fn, conv)
  return function(self, ...) as(self)[field] = conv(fn, ...) return self end
end
local function strConv(fn, s) return optStr(s, 2, fn) end
local function fnConv(fn, f) return optFn(f, 2, fn) end
for _, d in gipairs({{"title", "setTitle", strConv}, {"toggleTitle", "setToggleTitle", strConv},
                     {"color", "setColor", nullableVec3, "color"}, {"hoverColor", "setHoverColor", nullableVec3, "hoverColor"},
                     {"toggleColor", "setToggleColor", nullableVec3, "toggleColor"},
                     {"item", "setItem", itemArg, "item"}, {"hoverItem", "setHoverItem", itemArg, "hoverItem"},
                     {"toggleItem", "setToggleItem", itemArg, "toggleItem"},
                     {"leftClick", "setOnLeftClick", fnConv}, {"rightClick", "setOnRightClick", fnConv},
                     {"toggle", "setOnToggle", fnConv}, {"untoggle", "setOnUntoggle", fnConv},
                     {"scroll", "setOnScroll", fnConv}}) do
  local field, name, conv, errName = d[1], d[2], d[3], d[4]
  local short = name:sub(4, 4):lower() .. name:sub(5)          -- setTitle -> title, setOnLeftClick -> onLeftClick
  ActionM[name] = setter(field, errName or name, conv)
  ActionM[short] = setter(field, errName or short, conv)
end
for _, d in gipairs({{"texture", "setTexture"}, {"hoverTexture", "setHoverTexture"}, {"toggleTexture", "setToggleTexture"}}) do
  local field, name = d[1], d[2]
  local f = function(self, tex, u, v, w, h, scale) as(self)[field] = textureData(name, tex, u, v, w, h, scale) return self end
  ActionM[name], ActionM[field] = f, f
end
function ActionM.getTitle(self) local s = as(self) if s.toggled then return s.toggleTitle == nil and s.title or s.toggleTitle end return s.title end
function ActionM.getColor(self) return as(self).color end
function ActionM.getHoverColor(self) return as(self).hoverColor end
function ActionM.getToggleTitle(self) return as(self).toggleTitle end
function ActionM.getToggleColor(self) return as(self).toggleColor end
function ActionM.isToggled(self) return as(self).toggled end
function ActionM.setToggled(self, b) as(self).toggled = bool(b) return self end
ActionM.toggled = ActionM.setToggled
local actionFields = {leftClick = true, rightClick = true, toggle = true, untoggle = true, scroll = true}
local function actionIndex(self, k)
  if actionFields[k] then return as(self)[k] end
  return nil
end
local function actionNewIndex(self, k, v)
  if not actionFields[k] then gerror('Cannot assign value on key "' .. gtostring(k) .. '"', 2) end
  as(self)[k] = rawtype(v) == "function" and v or nil
end
local ActionMT = class("Action", ActionM, {index = actionIndex, newindex = actionNewIndex,
  tostring = function(self) local t = as(self).title return t == nil and "Action Wheel Action" or ("Action Wheel Action (" .. t .. ")") end})
local function newAction()
  local a = udata(ActionMT)
  AS[a] = {toggled = false}
  return a
end
local HOVER_COLOR, TOGGLE_COLOR = {1, 1, 1}, {0, 1, 0}
local function actionExecute(a, left)
  local s = AS[a]
  local f = left and s.leftClick or s.rightClick
  if f then f(a) end
  if not left then return end
  if s.toggled then f = s.untoggle or s.toggle else f = s.toggle end
  if f then
    s.toggled = not s.toggled
    f(s.toggled, a)
  end
end

local PageM = {}
local function ps(self) return PS[self] end
local function greatest(s) local g = 0 for i in gpairs(s.actions) do if i > g then g = i end end return g end
local function slots(s, shift)
  shift = shift or s.shift
  local r = {}
  for i = 0, 7 do r[i + 1] = s.actions[i + 8 * shift] end
  return r
end
local function checkIndex(s, index)
  if index ~= nil then
    if index < 1 then gerror("Index must be greater than 0!", 3) end
    return index - 1
  end
  local i = 0
  while s.actions[i] ~= nil do i = i + 1 end
  return i
end
local function setShift(s, shift)
  local g = mfloor(greatest(s) / 8) + 1
  s.shift = math.min(math.max(shift - 1, 0), g - 1)
end
function PageM.newAction(self, index)
  local s = ps(self)
  local a = newAction()
  s.actions[checkIndex(s, optInt(index, 2, "newAction", nil))] = a
  return a
end
function PageM.getAction(self, index)
  index = optInt(index, 2, "getAction", 0)
  if index < 1 then gerror("Index must be greater than 0!", 2) end
  return ps(self).actions[index - 1]
end
function PageM.setAction(self, index, action)
  local s = ps(self)
  index = optInt(index, 2, "setAction", nil)
  if action ~= nil and ftype(action) ~= "Action" then argErr(3, "setAction", "Action", action) end
  if index == nil or index == -1 then index = checkIndex(s, nil) + 1 end
  if index < 1 then gerror("Index must be greater than 0!", 2) end
  s.actions[index - 1] = action
  return self
end
PageM.action = PageM.setAction
function PageM.getSlotsShift(self) return ps(self).shift + 1 end
function PageM.setSlotsShift(self, shift) setShift(ps(self), optInt(shift, 2, "setSlotsShift", 0)) return self end
PageM.slotsShift = PageM.setSlotsShift
function PageM.getActions(self, shift)
  local s = ps(self)
  shift = optInt(shift, 2, "getActions", nil)
  if shift ~= nil then
    if shift < 1 then gerror("Shift must be greater than 0!", 2) end
    return slots(s, shift - 1)
  end
  local t = {}
  for i, a in gpairs(s.actions) do t[i + 1] = a end
  return t
end
function PageM.getTitle(self) return ps(self).title end
function PageM.setKeepSlots(self, b) ps(self).keepSlots = bool(b) return self end
function PageM.shouldKeepSlots(self) return ps(self).keepSlots end
local PageMT = class("Page", PageM, {
  index = function(self, k) if k == "keepSlots" then return ps(self).keepSlots end return nil end,
  newindex = function(self, k, v)
    if k == "keepSlots" then ps(self).keepSlots = bool(v) else gerror('Cannot assign value on key "' .. gtostring(k) .. '"', 2) end
  end,
  tostring = function(self) local t = ps(self).title return t ~= nil and (t .. " (Action Wheel Page)") or "Action Wheel Page" end})

local wheel = {pages = {}, current = nil, enabled = false, selected = -1}
local AWM = {}
-- gui ActionWheel.execute (index 0-based, left click)
local function wheelExecute(index, left)
  local f = left and wheel.leftClick or wheel.rightClick
  if f then
    local r = f()
    if r == true then return end
  end
  local p = wheel.current
  if index < 0 or index > 7 or p == nil then wheel.selected = -1 return end
  local a = slots(PS[p])[index + 1]
  if a ~= nil then actionExecute(a, left) end
  wheel.selected = -1
end
function AWM.execute(self, index, right)
  index = optInt(index, 2, "execute", nil)
  if index ~= nil and (index < 1 or index > 8) then gerror("index must be between 1 and 8", 2) end
  wheelExecute(index == nil and wheel.selected or index - 1, not bool(right))
  return self
end
function AWM.isEnabled(self) return wheel.enabled end
function AWM.getSelected(self) return wheel.selected + 1 end
function AWM.getSelectedAction(self)
  if wheel.current == nil or wheel.selected < 0 or wheel.selected > 7 then return nil end
  return slots(PS[wheel.current])[wheel.selected + 1]
end
function AWM.newAction(self) return newAction() end
function AWM.newPage(self, title)
  title = optStr(title, 2, "newPage")
  local p = udata(PageMT)
  PS[p] = {title = title, actions = {}, shift = 0, keepSlots = false}
  if title ~= nil then wheel.pages[title] = p end
  return p
end
function AWM.setPage(self, page)
  local cur
  if page == nil then cur = nil
  elseif ftype(page) == "Page" then cur = page
  elseif rawtype(page) == "string" then
    cur = wheel.pages[page]
    if cur == nil then gerror('Page "' .. page .. '" not found', 2) end
  else gerror('Invalid page type, expected "string" or "page"', 2) end
  if cur ~= nil and not PS[cur].keepSlots then setShift(PS[cur], 1) end
  wheel.current = cur
  return self
end
function AWM.getPage(self, title)
  title = optStr(title, 2, "getPage")
  if title ~= nil then return wheel.pages[title] end
  local t = {}
  for k, p in gpairs(wheel.pages) do t[k] = p end
  return t
end
function AWM.getCurrentPage(self) return wheel.current end
local wheelFields = {leftClick = true, rightClick = true, scroll = true}
local AWMT = class("ActionWheelAPI", AWM, {
  index = function(self, k) if wheelFields[k] then return wheel[k] end return nil end,
  newindex = function(self, k, v)
    if not wheelFields[k] then gerror('Cannot assign value on key "' .. gtostring(k) .. '"', 2) end
    wheel[k] = rawtype(v) == "function" and v or nil
  end})
action_wheel = udata(AWMT)
-- native side reads the wheel to draw it in the Bedrock Forge menu
local function colorOf(s, selected)
  if selected then return s.hoverColor or vec3(1, 1, 1) end
  if s.toggled then return s.toggleColor or vec3(0, 1, 0) end
  return s.color
end
reg.wheel(function()
  local p = wheel.current
  if not p then return nil end
  local st = PS[p]
  local out = {title = st.title, shift = st.shift + 1, groups = mfloor(greatest(st) / 8) + 1, selected = wheel.selected + 1}
  local sl = slots(st)
  local last = 0
  for i = 1, 8 do if sl[i] ~= nil then last = i end end
  for i = 1, last do
    local a = sl[i]
    if a then
      local s = AS[a]
      local title = ActionM.getTitle(a)
      local item = s.item
      if wheel.selected + 1 == i and s.hoverItem ~= nil then item = s.hoverItem
      elseif s.toggled and s.toggleItem ~= nil then item = s.toggleItem end
      out[i] = {title = title ~= nil and gtostring(title) or "", toggled = s.toggled == true,
                color = colorOf(s, wheel.selected + 1 == i), item = item,
                texture = (wheel.selected + 1 == i and s.hoverTexture) or (s.toggled and s.toggleTexture) or s.texture}
    else
      out[i] = {title = "", toggled = false, empty = true}
    end
  end
  return out
end)
-- gui ActionWheel.scroll(delta): wheel scroll function, then the selected action's scroll, else page shift
reg.wheelScroll(function(delta)
  if wheel.scroll then
    local r = wheel.scroll(delta)
    if r == true then return end
  end
  local p = wheel.current
  if not p then return end
  local st = PS[p]
  if wheel.selected >= 0 and wheel.selected <= 7 then
    local a = slots(st)[wheel.selected + 1]
    if a ~= nil and AS[a].scroll then AS[a].scroll(delta, a) return end
  end
  setShift(st, st.shift + 1 - (delta > 0 and 1 or delta < 0 and -1 or 0))
end)
-- gui state: enabled (wheel open) + selected slot (1-8, 0 = none)
reg.wheelState(function(enabled, selected)
  wheel.enabled = enabled and true or false
  wheel.selected = (tonumber(selected) or 0) - 1
end)

-- ---------------------------------------------------------------- config (ConfigAPI)
local cfgName = fb.avatarName()
local cfgTable, cfgLoaded = {}, false
local b64enc, b64dec = fb.b64enc, fb.b64dec
local function checkCfgName()
  if cfgName:find("[/\\]") or cfgName:find("%.%.") or cfgName:find("[%z:]") then
    gerror('Failed to parse file name "' .. cfgName .. '"', 3)
  end
end
local function isJavaInt(n) return n == mfloor(n) and n >= -2147483648 and n <= 2147483647 end
local function jstr(s) return nativeToJson(s) end
local writeArg
local function writeVec(v)
  local t = {}
  for i = 1, #v do t[i] = nativeToJson(v[i]) end
  return '{"type":"VECTOR","data":[' .. tconcat(t, ",") .. "]}"
end
writeArg = function(v)
  local t = rawtype(v)
  if t == "boolean" then return '{"type":"BOOL","data":' .. gtostring(v) .. "}"
  elseif t == "string" then return '{"type":"STRING","data":' .. jstr(b64enc(v)) .. "}"
  elseif t == "number" then
    if isJavaInt(v) then return '{"type":"INT","data":' .. sformat("%d", v) .. "}" end
    return '{"type":"DOUBLE","data":' .. nativeToJson(v) .. "}"
  elseif t == "table" then
    local items = {}
    for k, x in gnext, v do
      local val = writeArg(x)
      if val ~= nil then items[#items + 1] = '{"key":' .. (writeArg(k) or "null") .. ',"value":' .. val .. "}" end
    end
    return '{"type":"TABLE","data":[' .. tconcat(items, ",") .. "]}"
  elseif isVec(v) then return writeVec(v)
  elseif isMat(v) then
    local ok, r = pcall(function()
      local cols = {}
      local n = tonumber(ftype(v):sub(-1))
      for i = 1, n do cols[i] = writeVec(v:getColumn(i)) end
      return '{"type":"MATRIX","data":[' .. tconcat(cols, ",") .. "]}"
    end)
    return ok and r or nil
  end
  return nil
end
local function readVec(arr)
  local n = #arr
  if n == 2 then return vecs.vec2(arr[1], arr[2]) elseif n == 3 then return vecs.vec3(arr[1], arr[2], arr[3])
  elseif n == 4 then return vecs.vec4(arr[1], arr[2], arr[3], arr[4]) end
  return vecs.vec(tunpack(arr))
end
local readArg
readArg = function(j)
  local t, d = j.type, j.data
  if t == "BOOL" then return d == true
  elseif t == "INT" then return mfloor(d)
  elseif t == "DOUBLE" then return d
  elseif t == "STRING" then return b64dec(d)
  elseif t == "TABLE" then
    local r = {}
    for _, e in gipairs(d) do
      local k, v = readArg(e.key), readArg(e.value)
      if k ~= nil then r[k] = v end
    end
    return r
  elseif t == "VECTOR" then return readVec(d)
  elseif t == "MATRIX" then
    local cols = {}
    for i, c in gipairs(d) do cols[i] = readVec(c.data) end
    local f = mats and mats["mat" .. #cols]
    if f then return f(tunpack(cols)) end
    return nil
  end
  gerror("No enum constant " .. gtostring(t), 0)
end
local function cfgInit()
  if cfgLoaded then return end
  cfgTable = {}
  checkCfgName()
  local text = fb.cfgRead(cfgName)
  if text == nil then return end
  local ok, err = pcall(function()
    local root = parseJsonRaw(text)
    if root == nil then return end
    for k, v in gpairs(root) do cfgTable[k] = readArg(v) end
  end)
  if not ok then gerror("Failed to load avatar data file", 3) end
  cfgLoaded = true
end
local function cfgWrite()
  checkCfgName()
  local items = {}
  for k, v in gnext, cfgTable do items[#items + 1] = jstr(gtostring(k)) .. ":" .. (writeArg(v) or "null") end
  fb.cfgWrite(cfgName, "{" .. tconcat(items, ",") .. "}")
end
local function readOnly(t, seen)
  seen = seen or {}
  if seen[t] then return seen[t] end
  local copy = {}
  local proxy = setmetatable({}, {__index = copy, __newindex = function() gerror("table is read-only", 2) end,
    __len = function() return #copy end, __pairs = function() return gnext, copy, nil end, __metatable = false})
  seen[t] = proxy
  for k, v in gnext, t do copy[k] = rawtype(v) == "table" and readOnly(v, seen) or v end
  return proxy
end
local CfgM = {}
function CfgM.getName(self) return cfgName end
function CfgM.setName(self, n)
  cfgName = optStr(n, 2, "setName")
  if cfgName == nil then cfgName = "null" end
  cfgLoaded = false
  return self
end
CfgM.name = CfgM.setName
function CfgM.save(self, key, val)
  key = optStr(key, 2, "save")
  if key == nil then gerror("Cannot invoke \"String.length()\" because \"key\" is null", 2) end
  cfgInit()
  local t = rawtype(val)
  if not (t == "boolean" or t == "string" or t == "number" or t == "table" or isVec(val) or isMat(val)) then val = nil end
  cfgTable[key] = val
  cfgWrite()
  return self
end
function CfgM.load(self, key)
  key = optStr(key, 2, "load")
  cfgInit()
  if key ~= nil then return cfgTable[key] end
  return readOnly(cfgTable)
end
local CfgMT = class("ConfigAPI", CfgM)
config = udata(CfgMT)

-- ---------------------------------------------------------------- event dispatch from the host
reg.callEvent(function(name, ...)
  runPings() -- Avatar.flushQueuedEvents: queued pings run before the next event
  local e = evs[name]
  if e then return callEvent(e, ...) end
end)

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
