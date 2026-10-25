# Java Mod Inspector (experimental)

A read-only Java helper for the BedrockForge Java-to-Bedrock pipeline.

- **ASM 9.11.1** visits compiled class bytecode and reports GeckoLib 4/5 class references and call owners.
- **Fabric Mapping-IO** parses supplied Tiny, TSRG, or other supported mappings into a mapping tree; namespace count is reported for now. **No automatic remapping is implemented yet.**
- Requires Java 21 and Gradle. Dependencies resolve via Maven Central. The project does not download or run Minecraft mods by itself.

Run from this folder:

```powershell
gradle run --args='"C:\\testmods\\cave_dweller-1.6.4-neoforge-26.3.jar"'
gradle run --args='"C:\\testmods\\mod.jar" "C:\\mappings\\mapping.tiny"'
```

Output is machine-readable JSON on stdout. Treat third-party archives as untrusted. No JVM class loading or mod execution takes place.

## Vineflower CLI (separate optional tool)

Download Vineflower from https://github.com/Vineflower/vineflower/releases and verify its release/checksum. On an isolated folder:

```powershell
java -jar C:\\tools\\vineflower.jar C:\\testmods\\mod.jar C:\\analysis\\decompiled
```

**Never commit proprietary decompiled mod source**, and do not assume decompiled Java is equivalent to the original source. A JDK 21 runtime may not support decompiling future Java class versions; validate each mod's bytecode.

This helper is not wired into JavaModAnalyzer or the BedrockForge C++ runtime yet. It is an independent input for later conversion reports.
