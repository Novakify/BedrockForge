Lua 5.2.3 (MIT, Copyright (C) 1994-2013 Lua.org, PUC-Rio), unmodified sources from github.com/lua/lua tag v5.2.3
(lua.c and ltests.c removed). Used by FiguraBridge (src/figura) as the avatar script VM: Figura 0.1.6 runs Lua 5.2
(luaj), so avatar scripts expect 5.2 semantics. Compiled as C++ through src/figura/core/LuaAll.cpp so Lua errors
are C++ exceptions and unwind C++ frames safely.
