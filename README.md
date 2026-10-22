# From The Fog for Bedrock (private conversion)

Bedrock 1.26 conversion of **From The Fog** by **LunarEclipseStudios** (https://github.com/LunarEclipseStudios/From-The-Fog),
licensed **CC BY-NC-SA 4.0** - this conversion keeps the same licence: credit the original authors, non-commercial,
share-alike. Private project: the original's fork policy asks that *redistributed* forks not be AI generated, so this
conversion is never published or shared.

- `docs/SPEC.md` - full feature spec of the datapack (328 functions) with the Bedrock mapping.
- Horror atmosphere pack (planned): built on Newb X Legacy (MIT) with effects adapted from Cursed Fog (CC BY 4.0, LoLip_p)
  and Kefir's Flashlight (MIT, Kefirichik); credits kept in the pack.

## Packs (v1)

- `packs/FromTheFogBP` - behavior pack: `ftf:herobrine` and `ftf:door_ghost` entities, Fog Journal + heirloom disc items,
  converted structure templates (`structures/ftf/*.mcstructure`), Script API haunting core in `scripts/`
  (`@minecraft/server` 2.3.0, `@minecraft/server-ui` 2.0.0, plain ES modules, no build step).
- `packs/FromTheFogRP` - resource pack: Herobrine client entity (62 of the original skins as `ftf:skin` variants, with an
  emissive layer), player geometry (classic + slim), breathing/walk/punch/hold/tilt animations, sounds, particles, paintings, lang.
- `packs/PORTING_NOTES.md` - what was ported, what is partial, what is not, and why.
- `tools/build_rp.py` - regenerates skins, render controllers, client entity, sounds, icons, lang and `scripts/skins.js`
  from the Java repo: `python3 tools/build_rp.py <java_repo> .`
- `tools/nbt2mcstructure.py` - Java structure `.nbt` -> Bedrock `.mcstructure` (self-contained NBT reader/writer).

Enable both packs on a world (Beta APIs are not needed). Open the settings with `/ftf:config` (operators), the Fog Journal
item, or `/scriptevent ftf:config`. Debug helpers: `/scriptevent ftf:debug` (toggle log), `ftf:start` (start hauntings now),
`ftf:sighting <creeping|stalking|lurking|dwelling|nightmare|ghost_miner>`, `ftf:events` (run the 5-minute events now),
`ftf:remove`, and the custom sighting API `ftf:summon {"position":"creeping","spot_fov":70,...}`.
