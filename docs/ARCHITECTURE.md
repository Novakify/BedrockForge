# Architecture (v0 — provisional)

```
Layer 5  Tools       JavaModAnalyzer · ModPorter · AssetConverter · packager
Layer 4  Compat      Fabric/Forge concept maps → BF SDK calls (generated scaffolds)
Layer 3  World       Behavior/resource packs + Script API  ⇄  event bridge  ⇄ native client
Layer 2  BF SDK      CoreAPI EventAPI InputAPI UIAPI RenderingAPI CameraAPI AudioAPI ConfigAPI …
                     (stable public headers, NO engine types leak through)
Layer 1  Adapters    version-specific code per MC build (bf_adapter_1_26_51)
         Runtime     LeviLamina Client 26.51.x  (hooks, events, mod loading)
```

Key rules
- SDK headers never include `mc/...` engine headers. Engine access lives only in adapters.
- Each feature checks a capability flag at startup (Lamium pattern); unknown game build → feature pauses, game keeps running.
- World↔client bridge candidates (to research, not assumed): scoreboard/tag polling, custom packets on user-controlled servers, chat/command channel in local worlds.

Phase 1 only builds: CoreAPI (lifecycle, logging), EventAPI (wrap ll::event), InputAPI (keybind), ConfigAPI.
