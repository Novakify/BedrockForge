// FiguraBridge: the whole Lua 5.2.3 VM in one translation unit, compiled as C++ (errors = C++ exceptions, so a script
// error thrown through our C++ API functions runs their destructors). Sources: third_party/lua52 (MIT).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define LUA_CORE
#define LUA_LIB
// luaconf.h / llimits.h only define some number macros for these files; the unity build includes them once
#define lobject_c
#define lvm_c
#define ltable_c
#include "../../../third_party/lua52/lapi.c"
#include "../../../third_party/lua52/lcode.c"
#include "../../../third_party/lua52/lctype.c"
#include "../../../third_party/lua52/ldebug.c"
#include "../../../third_party/lua52/ldo.c"
#include "../../../third_party/lua52/ldump.c"
#include "../../../third_party/lua52/lfunc.c"
#include "../../../third_party/lua52/lgc.c"
#include "../../../third_party/lua52/llex.c"
#include "../../../third_party/lua52/lmem.c"
#include "../../../third_party/lua52/lobject.c"
#include "../../../third_party/lua52/lopcodes.c"
#include "../../../third_party/lua52/lparser.c"
#include "../../../third_party/lua52/lstate.c"
#include "../../../third_party/lua52/lstring.c"
#include "../../../third_party/lua52/ltable.c"
#include "../../../third_party/lua52/ltm.c"
#include "../../../third_party/lua52/lundump.c"
#include "../../../third_party/lua52/lvm.c"
#include "../../../third_party/lua52/lzio.c"
#include "../../../third_party/lua52/lauxlib.c"
#include "../../../third_party/lua52/lbaselib.c"
#include "../../../third_party/lua52/lbitlib.c"
#include "../../../third_party/lua52/lcorolib.c"
#include "../../../third_party/lua52/ldblib.c"
#include "../../../third_party/lua52/lmathlib.c"
#include "../../../third_party/lua52/lstrlib.c"
#include "../../../third_party/lua52/ltablib.c"
// not linked: liolib, loslib, loadlib, linit (Figura's sandbox has no io/os/package; we open libs ourselves)
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
