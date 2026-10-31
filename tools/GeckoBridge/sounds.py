"""GeckoBridge: a mob's own ambient / hurt / death / step sounds from the jar's bytecode.

Java mobs return their sounds from getAmbientSound (m_7515_), getHurtSound (m_7975_), getDeathSound (m_5592_) and
playStepSound (m_7355_). Mods hand back either a string-built SoundEvent (MCreator) or a field of their own sound
registry (ModSounds.BEAR_HURT.get()). We map those to the mod's sounds.json events; vanilla SoundEvents fields are
left to Bedrock's defaults (reported).
"""
from __future__ import annotations
import re, tempfile, zipfile
from pathlib import Path

import controllers as C

SOUND_METHODS = {"m_7515_": "ambient", "getAmbientSound": "ambient", "m_7975_": "hurt", "getHurtSound": "hurt",
                 "m_5592_": "death", "getDeathSound": "death", "m_7355_": "step", "playStepSound": "step"}


def sound_fields(tmp, classes):
    """Static fields of the mod's sound registry -> sounds.json event name (DeferredRegister.register("bear_hurt", ...))."""
    out = {}
    for cls in classes:
        raw = Path(tmp, cls.replace(".", "/") + ".class").read_bytes()
        if b"SoundEvent" not in raw or b"<clinit>" not in raw: continue
        ms = C.parse_methods(C.javap(tmp, cls))
        last = None
        for off, op, rest in ms.get("<clinit>", []):
            if op.startswith("ldc"):
                v = C.const_of(op, rest)
                if isinstance(v, str) and re.fullmatch(r"[a-z0-9_.]+", v): last = v
            if op == "putstatic" and last:
                own, nm, desc = C.member(rest)
                if "Sound" in desc or "RegistryObject" in desc or "Supplier" in desc or "Holder" in desc: out[nm] = last
                last = None
    return out


def mob_sounds(jar, entity_classes):
    """{entity class: {kind: sounds.json event}} plus {"_unmapped": [...]}."""
    tmp = tempfile.mkdtemp(prefix="gb_snd_")
    with zipfile.ZipFile(jar) as z:
        cls_files = [n for n in z.namelist() if n.endswith(".class") and not n.startswith(C.GL_PREFIXES)]
        for n in cls_files: z.extract(n, tmp)
    all_cls = [n[:-6].replace("/", ".") for n in cls_files]
    fields = sound_fields(tmp, all_cls)
    out, unmapped = {}, []
    for cls in entity_classes:
        seen, cur = set(), cls
        while cur and cur in all_cls and cur not in seen:       # mod superclasses (NaturalistAnimal...) too
            seen.add(cur)
            dis = C.javap(tmp, cur); ms = C.parse_methods(dis)
            for mname, code in ms.items():
                kind = SOUND_METHODS.get(mname.split("#")[0])
                if not kind or kind in out.get(cls, {}): continue
                ev, vanilla = None, None
                for off, op, rest in code:      # the LAST sound in the method = Java's default (else) branch
                    if op.startswith("ldc"):
                        v = C.const_of(op, rest)
                        if isinstance(v, str) and ":" in v: ev = v.split(":", 1)[1]
                    if op == "getstatic":
                        own, nm, desc = C.member(rest)
                        if nm in fields: ev = fields[nm]
                        elif "SoundEvent" in desc: vanilla = nm
                if not ev and vanilla: unmapped.append(f"{cls.split('.')[-1]}.{kind}: vanilla {vanilla}")
                if ev: out.setdefault(cls, {})[kind] = ev
            sup = re.search(r"class [\w.$]+(?:<[^>]*>)? extends ([\w.$]+)", dis)
            cur = sup.group(1) if sup else None
    out["_unmapped"] = unmapped
    return out
