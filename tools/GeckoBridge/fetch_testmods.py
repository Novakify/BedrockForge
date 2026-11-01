#!/usr/bin/env python3
"""Download the GeckoBridge test mods from Modrinth into a folder and check their sha512 (private use only).
    python -I tools/GeckoBridge/fetch_testmods.py C:/BedrockModding/testmods"""
import hashlib, sys, urllib.request
from pathlib import Path
MODS = [
 ("https://cdn.modrinth.com/data/70pj2OCh/versions/wt6Qw64V/cave_dweller-1.20.1-1.7.0.jar", "78e98458d7c0c85ee696faa2934689916d1712094151e4b37e2c05e98eaaf413aac05316bf060373462d84965264c35b5abaf9a2fb9370705e4912ea17063d30"),
 ("https://cdn.modrinth.com/data/tBbdvEJc/versions/C2sIsEEd/sculkhorde-1.20.1-0.12.7.jar", "e308995f167ec8e77cc309b50745716762ff994eb243b7ee4d0d56aa335f8764bb014935bc137837152ca32fb87d5cdf8f5e5401e07c6a4931143ba52ad1e801"),
 ("https://cdn.modrinth.com/data/F8BQNPWX/versions/wnR21jg1/naturalist-5.0pre4%2Bforge-1.20.1.jar", "d1eef3a3bcb3a73f1df4fe6f10103d21a3812d65e78ac6216bacf30e480d87d9151db5ec2eb9319295da818d9b4df5e4fe3c068f573a5e1088284f2bf6a02588"),
]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
for url, sha in MODS:
    name = urllib.request.unquote(url.rsplit("/", 1)[1]); data = urllib.request.urlopen(url).read()
    ok = hashlib.sha512(data).hexdigest() == sha
    if ok: (out / name).write_bytes(data)
    print(name, "OK" if ok else "SHA MISMATCH - not saved")
