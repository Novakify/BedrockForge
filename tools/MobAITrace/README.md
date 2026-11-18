# MobAITrace: Java-side recorder for MobAI parity (private dev tool)

Forge 1.20.1 mod: one JSON line per tick per mob of the traced mod (and per player) to `logs/mobai_trace.jsonl`
(tick, id, type, pos, velocity, hp, target, running goals + target goals with priorities).
Bedrock side: `/tag @s add gb_trace` in a world with a JavaBridge pack -> `[MobAITrace] {...}` lines in the content log
(flushed when you leave the world).

Build (no Gradle; SRG names compile against the Forge install's jars):
    CP=<client-1.20.1-*-srg.jar>:<forge-1.20.1-47.x-universal.jar>:<javafmllanguage>:<fmlcore>:<fmlloader>:<eventbus>
    javac --release 17 -proc:none -cp "$CP" -d out src/dev/novak/mobaitrace/MobAiTrace.java && cp -r res/* out/ && (cd out && jar cf ../mobaitrace-1.0.0.jar .)
Trace another mod: `-Dmobaitrace.prefix=<java package>.` in the instance's JVM args.
