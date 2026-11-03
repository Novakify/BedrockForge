# Offline Click Assist — additive version (experimental)

**Branch:** `feature/offline-click-assist`, based on `clientui/bf`. Not merged into the active client UI build.

## What the user requested
Restore the feel of a failing mouse's former 15–16 CPS, **without replacing manual clicking**. A click-boost module adds about 4–6 additional presses per second to normal manual left-clicks when a real tapping pattern is detected. The user's follow-up sets the **combined ceiling to 16 CPS, never above it**.

## UI / behavior
- **Right Shift > Mods > Combat > Click Assist (Offline)**, off by default.
- **Extra CPS** slider: 4, 5 (default), or 6.
- The module waits for **at least two actual left-button-down events** in a 1.5-second window and needs a real click within the last 1 second. Just holding down LMB after a single click will not arm it. This is intentionally an approximate activity heuristic, not a true physical-press classifier.
- It schedules at most the selected additional clicks per second, and will inject only while the combined one-second accepted click counter is **below 16**. Clicks beyond the combined cap are canceled while the eligible module is active. It waits for LMB to be physically released so it doesn't overlap a real down press.
- Genuine clicks and synthetic clicks are both counted toward the module's 16-CPS budget; the visible HUD counter may differ from actual attacks while the experimental input path is not fully validated.
- The scheduler skips missed opportunities after rendering stalls rather than dumping a burst of clicks.
- Generated down is balanced by an up on a later HUD frame, including if the module becomes ineligible during the event sequence.
- It never actively tries to exceed 16 CPS; actual combat actions may be lower due to input polling or combat rules.

## Safety boundaries
The native `clickAssistPermitted` check requires: an active, focused offline Survival world, neither `isMultiPlayerClient()` nor `isInMultiplayerGame()`, mouse grabbed, and no menu. Separate `CPS Cap` disables the assist. **Do not remove the offline guard or test the module on public servers.**

## Implementation notes
- Uses `Mouse::_instance().feed` and `MouseAction::ActionLeft`, not Windows `SendInput`. GDK `GameInput` may bypass this path; this is not yet proven in game.
- The `MouseInputEvent` callback counts physical presses to arm the scheduler; the boost itself records a synthetic press before calling `feed` so each injection spends its 16-CPS budget even when the input callback doesn't fire.
- If synthetic `feed` events are delivered asynchronously, exact attribution needs a more robust GameInput hook. Check that they cannot be misidentified as physical taps.
- A true guarantee of exactly 16 accepted attacks/second is impossible from a click queue alone, because the game may combine or drop input. The current implementation caps **scheduled and observed click-down events**; runtime validation must confirm what the actual game consumes.

## Validation
1. Build the isolated branch via GitHub Actions and back up the current client DLL.
2. Run a disposable **offline single-player Survival** world, with multiplayer disabled.
3. Try 10–12 real CPS, select +4 then +5 then +6, and verify **combined CPS never exceeds 16** in an independent observer, not just the integrated HUD.
4. Check that the mouse still behaves naturally at various speeds and that a held button without a tapping pattern does not start an autoclicker.
5. Verify real attack actions on a mob. If the CPS counter increases but no attack occurs, investigate the GameInput route.
6. Confirm safe stop on pause, focus change, leaving the world, module toggle, or any online session. Verify the cap does not unintentionally alter physical mouse input on multiplayer clients.

**Status:** Code committed on the feature branch, not yet verified by an in-game run or deployment. A green build alone is insufficient.
