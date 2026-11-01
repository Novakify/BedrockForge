# Bedrock Forge - rules for every session

## One live version: `main`
- `main` is THE Bedrock Forge build Fernando plays. Every finished feature (client UI, LyricsForge, Loadouts,
  Cinematic, ports...) must end up on `main`. No feature lives only on a side branch.
- Work on a short branch made FROM CURRENT `main` (`git fetch origin main` first), then merge it back into `main`
  as soon as it builds. Don't base new work on another feature branch.
- Before installing a DLL on the PC, check it was built from `main` (`builds` branch, BUILD_INFO.txt). If you must
  install a side-branch build for a test, merge that branch into `main` right after, so the next main build has it.
- Before merging into `main`: `git fetch origin main` and merge/rebase so nothing on main is lost.
- Research branches (`research/*`) hold tools/docs only; keep them off `main` until the plan is agreed.
- Renderer Foundation has its own line; same rule applies there (one live RF branch, everything merged into it).
- CI: a push to `main` publishes the full payload (DLL + resources/client + resources/lyricsflow) to `builds`.
