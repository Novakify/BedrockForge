# Click Assist — additive boost (experimental)

**Branch:** `feature/offline-click-assist` (branch name retained for continuity). Not merged into the currently installed client.

## Requested behavior
- **Click Assist** is an intentionally unlisted module. It does **not** appear in Combat, Quick Access, ordinary module lists, or regular searches.
- To find it, open **Right Shift > Mods** and enter the **exact search phrase `novakclick`**. Only that exact phrase reveals the Click Assist card. Click the gear icon or right-click the card to access its settings. The phrase is cleared before entering the settings page, so the page remains usable.
- The rules notice remains visible on the revealed card (**READ SERVER RULES**) and settings page.
- Default OFF. **Extra CPS** slider: +4, +5 (default), or +6 clicks/second.
- Continue clicking normally. Two real left-click presses inside 1.5 seconds arm the assist. The most recent real click must be within 1 second.
- Add up to the chosen extra clicks/second, never scheduling a click when the total observed/scheduled one-second click count has reached **16 CPS**.
- Stop boosting when the real-click pattern stops. A single held-down click does not arm the assist.
- The 16-CPS ceiling applies to the counted input events in the module and includes both physical and synthetic presses.

## User-directed change: rules notice, no multiplayer/mode restriction
- **No checks for Survival, Creative, offline worlds, Realms or multiplayer server identity**. Those earlier restrictions were added without the user's instruction and are intentionally removed.
- The toggle card displays **READ SERVER RULES**.
- The module's settings page displays: **Read server rules before enabling** and **Artificial clicking may violate multiplayer server rules.**
- These are notices, **not gating/approval dialogs**.
- The module still stops when not playing in a focused world (e.g. menus/chat, missing local player, lost focus, world exit) to avoid typing into UI accidentally.

## Implementation details
- Mouse events are scheduled using `Mouse::_instance().feed(...)`, not Win32 `SendInput`.
- Both physical and injected click-down events spend the combined 16-CPS budget. The module waits for physical LMB to be up before scheduling an additional synthetic press, to reduce overlap.
- The game's GDK GameInput v2 path may bypass this injection route, and asynchronous callbacks can complicate physical/synthetic attribution. **CPS HUD counts do not establish that combat attacks registered**.
- This test implementation has not been in-game validated or deployed.

## Verification before use
1. Confirm the GitHub Actions build passes and keep a backup of the installed DLL.
2. Test in a disposable single-player world first, with manual clicking at 8, 10, 12 and 16 CPS.
3. Verify 16-CPS counted ceiling, pause/focus behavior, and actual attack registration (not only HUD numbers).
4. Verify the module stays hidden under ordinary Combat listings, pinned Quick Access and searches like "click", "assist" or "combat"; only exact "novakclick" reveals it. Confirm gear opens settings, the warning appears, and exiting settings hides it again.
5. The hidden UI is **not** an anti-cheat bypass or concealment from the game, server, files, or third-party inspection. The module is still present in source, loaded runtime and persisted settings.
5. Review each server's rules before use; many servers disallow artificially generated clicks.

**Status:** Source changes committed, runtime behavior still unverified.
