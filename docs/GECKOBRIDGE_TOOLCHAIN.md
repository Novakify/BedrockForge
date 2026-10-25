# GeckoBridge toolchain and evidence-based report workflow

Branch: `geckobridge/compat-audit-20261025` — isolated from main.

## Purpose
Run source inspections before conversion, then add output comparison and runtime evidence. **Never report 100% converted based on a parsed JAR, a successful decompile, or matching JSON counts.**

## Components

| Tool | Role | Current integration |
| --- | --- | --- |
| Vineflower | Optional decompilation to readable Java | `tools/geckobridge_vineflower.py` explicit opt-in launcher; user supplies verified upstream jar |
| ASM 9.11.1 | Static method/type/field bytecode reference scanning | `tools/java-inspector/` Java 21 Gradle project; outputs JSON; does not load mod classes |
| Fabric Mapping-IO 0.8.0 | Read supported Java mapping formats | Java inspector accepts mapping file and reports parsed namespaces; does not remap Java methods yet |
| GeckoLib static analyzer | Detect GeckoLib 4/5 resources and bytecode namespace | `tools/geckobridge_audit.py` |
| Comparison | Compare identical animation IDs/bones/channels/loop flags/events | `tools/geckobridge_compare.py`; ID remap unsupported |
| Report | Generate structured JSON and human-readable MD | `tools/geckobridge_report.py`; always labels missing evidence |
| Smoke test | Synthetic JAR + JSON fixture; Java compile check | `.github/workflows/geckobridge-audit.yml` |

## Report command

```powershell
python tools/geckobridge_report.py --jar C:\\mods\\cave_dweller-1.6.4-neoforge-26.3.jar --out C:\\geckobridge-reports\\cave
python tools/geckobridge_report.py --jar C:\\mods\\cave_dweller-1.6.4-neoforge-26.3.jar --bedrock-rp C:\\converted\\CaveDwellerRP --out C:\\geckobridge-reports\\cave
```

The reports contain source SHA-256, GeckoLib inventory, optional tool configuration, source/output comparison and individually labelled status checks. They explicitly distinguish `not_tested`, `not_configured`, `inspected`, `compared` and `unavailable`. **No claim of complete conversion is made.**

## Optional advanced inspection

```powershell
cd tools/java-inspector
gradle run --args='"C:\\mods\\cave_dweller-1.6.4-neoforge-26.3.jar"'
cd ../..
python tools/geckobridge_vineflower.py --vineflower C:\\tools\\vineflower.jar --jar C:\\mods\\cave_dweller-1.6.4-neoforge-26.3.jar --out C:\\decompiled\\cave --run
```

Run decompilation only for JARs you are permitted to analyze. Keep output private and avoid committing redistributable third-party content.

## Status/limitations

This is an opt-in **analysis toolchain**, not yet a replacement for JavaModAnalyzer/ModPorter. It does not download Vineflower, remap bytecode, infer Java semantics, recreate mixins, translate controllers, execute conversion, or test Bedrock. Java tool dependencies will be downloaded by Gradle on first build. Independent Java and Python CI checks were added but have not yet been confirmed green. No game client code or game installation was modified.

## Claude integration steps

1. Find the existing JavaModAnalyzer and ModPorter implementations and decide how to call these tools from them.
2. Pin and verify external binary checksums, lock Gradle dependencies and supply legal mappings per targeted Java game version.
3. Add source-to-output ID maps, interpolation pose sampling and strict controller/event coverage.
4. Replace placeholder tool-presence statuses with provenance: command, version, tool log, output location and status.
5. Validate real conversion on Otherworld (GL4), Belowland (GL4) and Cave Dweller (GL5) in isolated output directories.
6. Keep Renderer Foundation, active shaderpacks and installed worlds untouched until hardware testing is authorized.

