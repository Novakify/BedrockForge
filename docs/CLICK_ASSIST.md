# Offline Click Assist (experimental)

**Branch:** `feature/offline-click-assist` based on `clientui/bf`. This is not merged into the active client UI branch.

## Intended behavior
- In the glass client menu: **Mods > Combat > Click Assist (Offline)**.
- Defaults **OFF** and uses the existing persisted `modules.json` settings.
- Hold the physical left mouse button to request up/down pulses at **16 CPS** by default; slider 8-20 CPS.
- The first physical click remains vanilla. The generated rate is subject to the frame rate, input update cadence and the game accepting synthetic events.
- The code uses `Mouse::_instance().feed(...)`, not Windows `SendInput`. Because this client uses GameInput v2, **the game action path must still be proven with a live test**; a HUD CPS count alone is not proof that an attack happened.

## Fail-closed safety guards
The module does not synthesize input unless ALL of the following hold on each HUD frame:
1. The module is enabled.
2. The local player is in Survival.
3. `IClientInstance::isMultiPlayerClient()` is false.
4. `IClientInstance::isInMultiplayerGame()` is false.
5. The mouse is grabbed in an active HUD world; the client is focused and the glass menu is closed.
6. Physical LMB is held.
7. The separate CPS Cap module is not enabled.

On pause/menu, missing focus, world exit, multiplayer, non-Survival mode or button release, the generator stops. The mode checks may conservatively block some local sessions; **do not relax them without verifying the session type**.

## Validation checklist
1. Build this branch with GitHub Actions. Install its test build, retaining a backup of the current `BedrockForgeCore.dll`.
2. Create an offline single-player Survival world with Multiplayer disabled, preferably a disposable test world.
3. Open Right Shift > Mods > Combat and turn on Click Assist. Hold LMB; check the existing CPS counter.
4. Check real interactions, including attacks on a test mob, rather than trusting the counter.
5. Confirm no actions occur while the mouse is released, when a menu/chat is open, in Creative, after leaving the world, or on an external server. Do not test the attack assist on public servers.
6. If CPS events appear without attacks, the 26.51 GDK `GameInput` path may bypass this native feed; investigate a controlled GameInput mouse-state integration on an isolated branch rather than claiming success.

No in-game test, binary deploy or server-gate validation has been performed as part of this change.
