# Known Limitations

- Native mods are tied to one exact game build (1.26.51.01). Every MC update needs a matching LeviLamina release.
- Windows x64 only. No Android/console.
- Public servers deferred; no anti-cheat bypass, ever.
- Java .jar files cannot run directly — they need Minecraft Java's engine, not just a JVM.
- BetterRenderDragon / Amethyst don't match our target version.
- PreLoader/PeEditor are closed-source under LeviMC EULA.
- Impact frames v0: no real camera shake yet (needs a camera hook; Lamium's camera code is the reference). Speed lines are axis-aligned streaks, not true radial lines (UI context only draws rectangles; radial needs RenderingAPI). Works only where the integrated server runs in-process (single-player / LAN host), because it listens to ActorHurtEvent.
- Impact sound uses a vanilla sound id (`random.anvil_land`), changeable in config. Custom sounds need a resource pack.
