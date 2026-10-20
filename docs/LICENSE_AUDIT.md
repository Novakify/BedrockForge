# License Audit

| Project | License | Commit | What we may do | Watch out |
|---|---|---|---|---|
| LeviLamina | LGPL-3.0 (+GPL text) | 4ca13569 | Link our mods/SDK against it; our code can use our own license | Changes *to LeviLamina itself* must stay LGPL and be published if distributed |
| PreLoader / PeEditor | LeviMC closed-source EULA | — | Use as shipped | No reverse engineering / decompiling |
| Lamium | LGPL-3.0 | f94e0b9 | Read, learn patterns; link-level reuse OK | Copying files into our SDK makes those files LGPL — avoid, rewrite instead, or keep as separate LGPL module with attribution |
| LeviLauncher | GPL-3.0-only | 64f5878 | Use as a tool | Don't copy its code into our SDK |
| Amethyst | Apache-2.0 (open parts); 1.26.x partly closed | e9179a6 | Reuse open code with attribution + NOTICE | Closed parts: no reuse |
| BetterRenderDragon | AGPL-3.0 | fc4e663 | Read for concepts | Any copied code forces AGPL on the whole distributed work — don't copy |
| levilamina-mod-template | CC0-1.0 | 3df96f2 | Anything | Replace LICENSE for our mods |
| Blueberry Client | Proprietary | — | Public docs only | No RE, no extraction |

**Our own license:** not yet chosen. Recommendation to decide later: LGPL-3.0 or MIT for the SDK. Needs your call before anything is published.
