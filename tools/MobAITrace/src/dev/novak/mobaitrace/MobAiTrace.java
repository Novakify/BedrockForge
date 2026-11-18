package dev.novak.mobaitrace;

import java.io.*;
import java.util.*;
import java.util.stream.Collectors;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.goal.GoalSelector;
import net.minecraft.world.entity.ai.goal.WrappedGoal;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.entity.living.LivingEvent;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.util.ObfuscationReflectionHelper;

/** BedrockForge MobAI parity recorder (private dev tool). Writes one JSON line per tick per mob of the
 *  traced mod (and per player) to logs/mobai_trace.jsonl: tick, id, type, pos, velocity, health, target,
 *  running goals with priorities. Compare with the Bedrock GoalRunner trace (docs/PORT_MOBAI.md, W4). */
@Mod("mobaitrace")
public class MobAiTrace {
    static final String PREFIX = System.getProperty("mobaitrace.prefix", "net.mcreator.thewonderland.");
    static PrintWriter out;
    static long started = System.currentTimeMillis();

    public MobAiTrace() {
        try { new File("logs").mkdirs(); out = new PrintWriter(new BufferedWriter(new FileWriter("logs/mobai_trace.jsonl", false)), true); }
        catch (IOException e) { throw new RuntimeException(e); }
        MinecraftForge.EVENT_BUS.addListener(MobAiTrace::onTick);
    }

    static String goals(Mob m, String field) {
        try {
            GoalSelector gs = ObfuscationReflectionHelper.getPrivateValue(Mob.class, m, field);
            return gs.m_25386_().map(w -> "\"" + w.m_26012_() + ":" + name(w.m_26015_()) + "\"").collect(Collectors.joining(","));
        } catch (Throwable t) { return ""; }
    }
    static String name(Object g) {
        Class<?> c = g.getClass(); String n = c.getName();
        if (n.contains("$")) n = n.substring(n.lastIndexOf('.') + 1) + "<" + c.getSuperclass().getSimpleName() + ">";
        else n = c.getSimpleName();
        return n;
    }

    static void onTick(LivingEvent.LivingTickEvent ev) {
        LivingEntity e = ev.getEntity();
        if (e.m_9236_().f_46443_) return;                       // server side only
        String cls = e.getClass().getName();
        boolean player = cls.endsWith("ServerPlayer");
        if (!player && !cls.startsWith(PREFIX)) return;
        long tick = e.m_9236_().m_46467_();                    // game time
        StringBuilder s = new StringBuilder(200);
        s.append("{\"t\":").append(tick).append(",\"id\":").append(e.m_19879_()).append(",\"type\":\"").append(e.getClass().getSimpleName())
         .append("\",\"x\":").append(r(e.m_20185_())).append(",\"y\":").append(r(e.m_20186_())).append(",\"z\":").append(r(e.m_20189_()))
         .append(",\"vx\":").append(r(e.m_20184_().f_82479_)).append(",\"vz\":").append(r(e.m_20184_().f_82481_))
         .append(",\"hp\":").append(r(e.m_21223_()));
        if (e instanceof Mob m) {
            LivingEntity t = m.m_5448_();
            s.append(",\"target\":").append(t == null ? "null" : "\"" + t.getClass().getSimpleName() + "#" + t.m_19879_() + "\"");
            s.append(",\"goals\":[").append(goals(m, "f_21345_")).append("],\"targets\":[").append(goals(m, "f_21346_")).append("]");
        }
        out.println(s.append("}"));
    }
    static String r(double v) { return String.valueOf(Math.round(v * 100) / 100.0); }
}
