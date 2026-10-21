"""lazurite compatibility shim for Minecraft 1.26 materials.

1.26.x material.bin files are format version 26 like late 1.21.x ones, but their buffer records no longer carry
the 4-byte value lazurite reads after `binding_slot` for version >= 26. We parse with the 1.21 layout first and
fall back to the 1.26 layout, remembering which one worked so `pack` writes the same layout back.
"""
import lazurite.material.buffer as _B
import lazurite.material.material as _M
from lazurite.material import Material  # noqa: F401  (re-export)

_LAYOUT = {"buffer_unknown": True}
_orig_read, _orig_write = _B.Buffer.read, _B.Buffer.write


def _ver(version):
    # pretend to be version 25 for the buffer record when the 1.26 layout is active
    return version if _LAYOUT["buffer_unknown"] else min(version, 25)


def _read(self, f, version):
    return _orig_read(self, f, _ver(version))


def _write(self, f, version):
    return _orig_write(self, f, _ver(version))


_B.Buffer.read, _B.Buffer.write = _read, _write


def load(path):
    last = None
    for layout in (True, False):
        _LAYOUT["buffer_unknown"] = layout
        try:
            m = _M.Material.load_bin_file(path)
            m._bf_layout = layout
            return m
        except Exception as e:  # wrong layout -> garbage strings / bad magic
            last = e
    raise last


def save(material, path):
    _LAYOUT["buffer_unknown"] = getattr(material, "_bf_layout", True)
    with open(path, "wb") as f:
        material.write(f)
