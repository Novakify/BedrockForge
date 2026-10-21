# Permission record

- 2026-10-21: Fernando (Novak) states he has tapeQ's permission to use Shine 3.0 to create a Bedrock conversion.
- TODO: save the written permission here (screenshot / message text, date, who granted it, exact scope).
  Shine's licence requires *prior written permission* for modifications.
- 2026-10-21: jar obtained from CurseForge (file 8659331, shine-3.0.jar, sha1 ce8bfae3feda3e6765d315779b8940ac957c3c44). Converted assets stay in build/ (gitignored).
- 2026-10-21: permission text attributed to tapeQ, supplied by Novak in a recovery zip (the file itself says it is a
  proposed record, not an authenticated signature). Scope as written:
  > "I give Novak permission to use and modify Shine 3.0's source code to work on a Minecraft Bedrock Edition conversion.
  > This permission is for private use, learning, development, and testing only. Novak may not publish, share, distribute,
  > or release the converted project or its source code publicly without contacting me and getting my permission again first."
  -> Working rule: PRIVATE ONLY. Nothing from this branch is published or shared. Still wanted: tapeQ's actual 3.0 source.
- The recovery zips contain the compiled classes + a partial javap disassembly (no .java). Not used: reconstructing code
  from bytecode was blocked in this session; conversion waits for real source or proceeds from readable assets/configs.
