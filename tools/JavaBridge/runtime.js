// JavaBridge runtime (original Bedrock Forge code). Runs Java mod code that jvm2js.py translated, by giving it a
// small Minecraft/Forge/JDK surface on top of the Bedrock Script API. Unknown calls are logged once and return a
// neutral value, so a missing API never stops a procedure.
import { world, system, EntityDamageCause, ItemStack as BItemStack, BlockPermutation } from "@minecraft/server";

export const CFG = globalThis.__JB_CFG;      // set by the generated module before import
const NS = CFG.ns, MODID = CFG.modid;
const warned = new Set();
function warn(k) { if (warned.has(k)) return; warned.add(k); if (warned.size < 400) console.warn(`[JavaBridge] unsupported ${k}`); }

// ------------------------------------------------------------------------------------------ mod class registry (M)
const FN = new Map(), CLS = new Map(), STATIC = new Map();
export const M = {
  def(id, fn) { FN.set(id, fn); },
  cls(name, sup, methods) { CLS.set(name, { sup, methods: new Set(methods) }); },
  call(id, args) {
    const f = FN.get(id);
    if (!f) { warn("mod method " + id); return null; }
    return f(...args);
  },
  miss(id) { return (...a) => { warn("mod method " + id); return null; }; },
  fcache: new Map(),
  find(cls, key) {
    let m = M.fcache.get(cls); if (!m) { m = new Map(); M.fcache.set(cls, m); }
    let r = m.get(key); if (r === undefined) { r = M.find0(cls, key); m.set(key, r); }
    return r;
  },
  find0(cls, key) {
    let c = cls, n = 0;
    while (c && CLS.has(c) && n++ < 12) { if (CLS.get(c).methods.has(key) && FN.has(c + "." + key)) return c + "." + key; c = CLS.get(c).sup; }
    return null;
  },
  vcall(o, key, api, args, owner) {
    if (o == null) return null;
    const cls = o.__cls ?? (o instanceof JEntity ? o.modClass() : null);
    if (cls) { const id = M.find(cls, key); if (id) return FN.get(id)(o, ...args); }
    return R.call(o, api, args, owner);
  },
  sget(cls, f) {
    const k = cls + "." + f;
    if (STATIC.has(k)) return STATIC.get(k);
    const reg = R.registryField(cls, f); if (reg !== undefined) return reg;
    M.clinit(cls);
    return STATIC.has(k) ? STATIC.get(k) : null;
  },
  sput(cls, f, v) {
    if (v && typeof v === "object" && v.__acc && !v.name) v.name = f;
    STATIC.set(cls + "." + f, v);
  },
  inited: new Set(),
  clinit(cls) {
    if (M.inited.has(cls)) return; M.inited.add(cls);
    if (FN.has(cls + ".<clinit>()V")) { try { FN.get(cls + ".<clinit>()V")(); } catch (e) { warn("clinit " + cls + " " + e); } }
  },
};

// ------------------------------------------------------------------------------------------ value types
export class Vec { constructor(x, y, z) { this.x_ = +x; this.y_ = +y; this.z_ = +z; }
  x() { return this.x_; } y() { return this.y_; } z() { return this.z_; }
  getX() { return this.x_; } getY() { return this.y_; } getZ() { return this.z_; }
  add(a, b, c) { return typeof a === "object" ? new Vec(this.x_ + a.x_, this.y_ + a.y_, this.z_ + a.z_) : new Vec(this.x_ + a, this.y_ + b, this.z_ + c); }
  subtract(a, b, c) { return typeof a === "object" ? new Vec(this.x_ - a.x_, this.y_ - a.y_, this.z_ - a.z_) : new Vec(this.x_ - a, this.y_ - b, this.z_ - c); }
  scale(s) { return new Vec(this.x_ * s, this.y_ * s, this.z_ * s); }
  multiply(a, b, c) { return new Vec(this.x_ * a, this.y_ * b, this.z_ * c); }
  length() { return Math.hypot(this.x_, this.y_, this.z_); }
  normalize() { const l = this.length() || 1; return this.scale(1 / l); }
  distanceTo(o) { return Math.hypot(this.x_ - o.x_, this.y_ - o.y_, this.z_ - o.z_); }
  distanceToSqr(a, b, c) { const o = typeof a === "object" ? a : new Vec(a, b, c); return (this.x_ - o.x_) ** 2 + (this.y_ - o.y_) ** 2 + (this.z_ - o.z_) ** 2; }
  horizontalDistanceSqr() { return this.x_ ** 2 + this.z_ ** 2; } horizontalDistance() { return Math.hypot(this.x_, this.z_); }
  above(n = 1) { return new Vec(this.x_, this.y_ + n, this.z_); } below(n = 1) { return new Vec(this.x_, this.y_ - n, this.z_); }
  north(n = 1) { return new Vec(this.x_, this.y_, this.z_ - n); } south(n = 1) { return new Vec(this.x_, this.y_, this.z_ + n); }
  east(n = 1) { return new Vec(this.x_ + n, this.y_, this.z_); } west(n = 1) { return new Vec(this.x_ - n, this.y_, this.z_); }
  offset(a, b, c) { return this.add(a, b, c); } relative(dir, n = 1) { return this.add(dir.dx * n, dir.dy * n, dir.dz * n); }
  getCenter() { return new Vec(this.x_ + 0.5, this.y_ + 0.5, this.z_ + 0.5); } immutable() { return this; }
  toB() { return { x: this.x_, y: this.y_, z: this.z_ }; }
}
const V = (x, y, z) => new Vec(x, y, z);
const bp = (x, y, z) => new Vec(Math.floor(x), Math.floor(y), Math.floor(z));
class AABB { constructor(a, b, c, d, e, f) { this.min = V(Math.min(a, d), Math.min(b, e), Math.min(c, f)); this.max = V(Math.max(a, d), Math.max(b, e), Math.max(c, f)); }
  inflate(x, y = x, z = x) { return new AABB(this.min.x_ - x, this.min.y_ - y, this.min.z_ - z, this.max.x_ + x, this.max.y_ + y, this.max.z_ + z); }
  getCenter() { return V((this.min.x_ + this.max.x_) / 2, (this.min.y_ + this.max.y_) / 2, (this.min.z_ + this.max.z_) / 2); }
  intersects(o) { return this.min.x_ < o.max.x_ && this.max.x_ > o.min.x_ && this.min.y_ < o.max.y_ && this.max.y_ > o.min.y_ && this.min.z_ < o.max.z_ && this.max.z_ > o.min.z_; }
}
class JList extends Array {
  static of(xs) { const l = new JList(); for (const x of xs) l.push(x); return l; }
  isEmpty() { return this.length === 0; } size() { return this.length; } get(i) { return this[i]; } add(x) { this.push(x); return true; }
  stream() { return JList.of(this); } iterator() { let i = 0; const a = this; return { hasNext: () => i < a.length, next: () => a[i++] }; }
  sorted(c) {
    if (c?.keyFn) { const k = new Map(); for (const x of this) k.set(x, R.lam(c.keyFn, x)); return JList.of([...this].sort((a, b) => k.get(a) - k.get(b))); }
    const l = JList.of(this); if (c) l.sort((a, b) => c.compare(a, b)); return l;
  }
  filter(p) { return JList.of(Array.prototype.filter.call(this, (x) => R.lam(p, x))); }
  map(f) { return JList.of(Array.prototype.map.call(this, (x) => R.lam(f, x))); }
  findFirst() { return new JOpt(this.length ? this[0] : null); } findAny() { return this.findFirst(); }
  toList() { return JList.of(this); } collect() { return JList.of(this); } count() { return this.length; }
  contains(x) { return this.some((y) => R.same(x, y) || R.equals(x, y)); } clear() { this.length = 0; }
  forEach(f) { for (const x of [...this]) R.lam(f, x); } anyMatch(p) { return this.some((x) => R.lam(p, x)); }
  set(i, v) { const o = this[i]; this[i] = v; return o; } remove(x) { const i = typeof x === "number" ? x : this.indexOf(x); if (i >= 0) return this.splice(i, 1)[0]; return null; }
}
class JOpt { constructor(v) { this.v = v; } orElse(d) { return this.v ?? d; } isPresent() { return this.v != null; } get() { return this.v; }
  ifPresent(f) { if (this.v != null) R.lam(f, this.v); } orElseGet(f) { return this.v ?? R.lam(f); } map(f) { return new JOpt(this.v == null ? null : R.lam(f, this.v)); } }

// ------------------------------------------------------------------------------------------ entities / levels
const ECACHE = new Map();
const EFIELDS = new Map();
export class JEntity {
  constructor(e) { this.e = e; }
  static of(e) { if (!e) return null; let w = ECACHE.get(e.id); if (!w || w.e !== e) { w = new JEntity(e); ECACHE.set(e.id, w); } return w; }
  modClass() { if (this._mc === undefined) this._mc = CFG.typeToClass[this.e.typeId] ?? null; return this._mc; }
  get __fields() { let f = EFIELDS.get(this.e.id); if (!f) { f = { animationprocedure: "empty" }; EFIELDS.set(this.e.id, f); } return f; }
  valid() { try { return this.e.isValid; } catch { return false; } }
  loc() { try { return this.e.location; } catch { return { x: 0, y: 0, z: 0 }; } }
  getX() { return this.loc().x; } getY() { return this.loc().y; } getZ() { return this.loc().z; }
  position() { const l = this.loc(); return V(l.x, l.y, l.z); } blockPosition() { const l = this.loc(); return bp(l.x, l.y, l.z); }
  getEyePosition() { try { const h = this.e.getHeadLocation(); return V(h.x, h.y, h.z); } catch { return this.position().add(0, 1.6, 0); } }
  level() { try { return JLevel.of(this.e.dimension); } catch { return JLevel.of(world.getDimension("minecraft:overworld")); } }
  getLevel() { return this.level(); } m_9236_() { return this.level(); }
  discard() { try { if (this.e.typeId !== "minecraft:player") this.e.remove(); } catch {} }
  remove() { this.discard(); } kill() { try { this.e.kill(); } catch {} }
  isAlive() { try { return this.e.isValid && (this.e.getComponent("health")?.currentValue ?? 1) > 0; } catch { return false; } }
  distanceToSqr(a, b, c) { const l = this.loc(); const o = a instanceof JEntity ? a.loc() : a instanceof Vec ? a.toB() : { x: a, y: b, z: c }; return (l.x - o.x) ** 2 + (l.y - o.y) ** 2 + (l.z - o.z) ** 2; }
  distanceTo(o) { return Math.sqrt(this.distanceToSqr(o)); }
  getYRot() { try { return this.e.getRotation().y; } catch { return 0; } } getXRot() { try { return this.e.getRotation().x; } catch { return 0; } }
  setYRot(y) { try { this.e.setRotation({ x: this.getXRot(), y }); } catch {} } setXRot(x) { try { this.e.setRotation({ x, y: this.getYRot() }); } catch {} }
  setYBodyRot(y) { this.setYRot(y); } setYHeadRot(y) { this.setYRot(y); } getYHeadRot() { return this.getYRot(); }
  getLookAngle() { try { const v = this.e.getViewDirection(); return V(v.x, v.y, v.z); } catch { return V(0, 0, 1); } }
  getViewVector() { return this.getLookAngle(); }
  getBbHeight() { try { return this.e.getComponent("collision_box")?.height ?? 1.8; } catch { return 1.8; } }
  getBbWidth() { try { return this.e.getComponent("collision_box")?.width ?? 0.6; } catch { return 0.6; } }
  getBoundingBox() { const l = this.loc(), w = this.getBbWidth() / 2, h = this.getBbHeight(); return new AABB(l.x - w, l.y, l.z - w, l.x + w, l.y + h, l.z + w); }
  getMoveControl() { return MoveCtl.of(this); }
  doHurtTarget(t) {   // Java Mob.doHurtTarget: melee hit with the mob's attack damage (+ the mod's own attack hook runs from entityHitEntity)
    if (!t?.e) return false; const now = system.currentTick; if ((this.__fields.__lastHit ?? -99) > now - 10) return false; this.__fields.__lastHit = now;
    let dmg = 2; try { dmg = this.e.getComponent("attack")?.damage?.rangeMin ?? this.e.getComponent("attack")?.damage ?? 2; } catch {}
    try { return t.e.applyDamage(+dmg || 2, { cause: "entityAttack", damagingEntity: this.e }); } catch { return false; }
  }
  getStringUUID() { return this.e.id; } getUUID() { return this.e.id; } getId() { return this.e.id; }
  getDirection() { const y = ((this.getYRot() % 360) + 360) % 360; return y < 45 || y >= 315 ? DIR.SOUTH : y < 135 ? DIR.WEST : y < 225 ? DIR.NORTH : DIR.EAST; }
  onGround() { try { return this.e.isOnGround; } catch { return true; } } isInWater() { try { return this.e.isInWater; } catch { return false; } }
  isShiftKeyDown() { try { return this.e.isSneaking; } catch { return false; } } setShiftKeyDown(v) { try { this.e.isSneaking = !!v; } catch {} }
  isSprinting() { try { return this.e.isSprinting; } catch { return false; } } isPassenger() { try { return !!this.e.getComponent("riding"); } catch { return false; } }
  isSleeping() { try { return this.e.isSleeping; } catch { return false; } }
  stopRiding() { try { this.e.getComponent("riding")?.entityRidingOn?.getComponent("rideable")?.ejectRider(this.e); } catch {} }
  setCustomName(c) { try { this.e.nameTag = String(c ?? ""); } catch {} } getDisplayName() { return this.e.nameTag || this.e.typeId.split(":")[1]; }
  getName() { return this.getDisplayName(); } hasCustomName() { try { return !!this.e.nameTag; } catch { return false; } }
  setDeltaMovement(a, b, c) { const v = a instanceof Vec ? a : V(a, b, c); try { this.e.clearVelocity(); this.e.applyImpulse(v.toB()); } catch {} }
  getDeltaMovement() { try { const v = this.e.getVelocity(); return V(v.x, v.y, v.z); } catch { return V(0, 0, 0); } }
  push(a, b, c) { try { this.e.applyImpulse({ x: a, y: b, z: c }); } catch {} }
  teleportTo(a, b, c, d, e2, f, g) {
    // Entity.teleportTo(x,y,z) | ServerPlayer.teleportTo(ServerLevel, x,y,z, yaw, pitch)
    try {
      if (a instanceof JLevel) this.e.teleport({ x: b, y: c, z: d }, { dimension: a.d, rotation: { x: f ?? 0, y: e2 ?? 0 } });
      else this.e.teleport({ x: a, y: b, z: c });
    } catch {}
  }
  moveTo(x, y, z) { this.teleportTo(x, y, z); } setPos(x, y, z) { this.teleportTo(x, y, z); }
  lookAt(anchor, v) {
    // Java Entity.lookAt only turns the head/body. The old teleport(facingLocation) re-placed the mob every tick and
    // killed its movement (stalkers "froze" while they watched you): set the rotation instead.
    try { const t = v instanceof Vec ? v.toB() : v instanceof JEntity ? v.e.getHeadLocation() : v; const f = this.e.getHeadLocation?.() ?? this.e.location;
      const dx = t.x - f.x, dy = t.y - f.y, dz = t.z - f.z;
      this.e.setRotation({ x: -Math.atan2(dy, Math.hypot(dx, dz)) * 180 / Math.PI, y: Math.atan2(-dx, dz) * 180 / Math.PI }); } catch {}
  }
  hurt(src, amt) {
    try { return this.e.applyDamage(amt, { cause: src?.cause ?? EntityDamageCause.entityAttack, ...(src?.attacker ? { damagingEntity: src.attacker.e } : {}) }); } catch { return false; }
  }
  setHealth(h) { try { this.e.getComponent("health").setCurrentValue(h); } catch {} }
  getHealth() { try { return this.e.getComponent("health").currentValue; } catch { return 0; } }
  getMaxHealth() { try { return this.e.getComponent("health").effectiveMax; } catch { return 20; } }
  addEffect(inst) { return R.applyEffect(this, inst); }
  hasEffect(eff) { return R.hasEffect(this, eff); }
  removeEffect(eff) { return R.removeEffect(this, eff); }
  getEffect(eff) { return R.getEffect(this, eff); }
  getActiveEffects() { try { return JList.of(this.e.getEffects().map((x) => ({ effect: x.typeId, getDuration: () => x.duration }))); } catch { return new JList(); } }
  updateSwingTime() {} refreshDimensions() {} swing() {}
  // portal travel is done by the ModPorter world generator's portal handler (same blocks); Java's own path stays idle
  isOnPortalCooldown() { return true; } setPortalCooldown() {} changeDimension() { return this; } handleInsidePortal() {}
  removeAllEffects() { try { for (const x of this.e.getEffects()) this.e.removeEffect(x.typeId); } catch {} }
  getPersistentData() { return new Tag(this, "pd"); }
  getEntityData() { return new SynchedData(this); } m_20088_() { return this.getEntityData(); }
  getCapability(cap) { return new JOpt(new PlayerVars(this)); }
  getTarget() { try { return JEntity.of(this.e.target); } catch { return null; } }
  setTarget(t) { R.setTarget(this, t); }
  getNavigation() { return new Nav(this); }
  getMainHandItem() { return R.handItem(this, "mainhand"); } getOffhandItem() { return R.handItem(this, "offhand"); }
  getItemBySlot(slot) { return R.handItem(this, slot?.slot ?? "mainhand"); } getItemInHand(h) { return R.handItem(this, h?.hand ?? "mainhand"); }
  setItemSlot(slot, stack) { R.setSlot(this, slot?.slot ?? "mainhand", stack); } setItemInHand(h, stack) { R.setSlot(this, h?.hand ?? "mainhand", stack); }
  getInventory() { return new Inv(this); }
  getAbilities() { return { instabuild: R.gamemode(this) === "creative", mayfly: R.gamemode(this) !== "survival", flying: false, invulnerable: R.gamemode(this) === "creative" }; }
  getCooldowns() { return new Cooldowns(this); }
  hasLineOfSight(o) { try { const hit = this.e.getEntitiesFromViewDirection({ maxDistance: 64 }); return true; } catch { return true; } }
  displayClientMessage(c, actionbar) { try { if (actionbar) this.e.onScreenDisplay.setActionBar(R.text(c)); else this.e.sendMessage(R.text(c)); } catch {} }
  sendSystemMessage(c) { this.displayClientMessage(c, false); }
  getGameProfile() { return { getId: () => this.e.id, getName: () => this.e.name }; }
  getRespawnPosition() { return null; } getRespawnDimension() { return { key: "minecraft:overworld" }; }
  setMaxUpStep() {} getStepHeight() { return 0.6; } setAirSupply() {} setNoGravity(v) { R.flag(this, "nograv", v); }
  isCreative() { return R.gamemode(this) === "creative"; } isSpectator() { return R.gamemode(this) === "spectator"; }
  getType() { return { etype: this.e.typeId, id: this.e.typeId }; } getEncodeId() { return this.e.typeId; }
  setSecondsOnFire(s) { try { this.e.setOnFire(s); } catch {} } clearFire() { try { this.e.extinguishFire(); } catch {} }
  addTag(t) { try { return this.e.addTag(t); } catch { return false; } } getTags() { try { return JList.of(this.e.getTags()); } catch { return new JList(); } }
  swing() {} startRiding(o) { try { o.e.getComponent("rideable")?.addRider(this.e); } catch {} }
  getRandom() { return RANDOM; }
  setInvisible(v) { try { v ? this.e.addEffect("invisibility", 20000000, { showParticles: false }) : this.e.removeEffect("invisibility"); } catch {} }
  isInvisible() { try { return !!this.e.getEffect("invisibility"); } catch { return false; } }
  setGlowingTag() {} setSilent() {} setInvulnerable() {} setPersistenceRequired() {}
  getServer() { return SERVER; } m_20194_() { return SERVER; }
}
const ICACHE = new Map();
export class JLevel {
  constructor(d) { this.d = d; }
  static of(d) { if (!d) return null; let w = ICACHE.get(d.id); if (!w) { w = new JLevel(d); ICACHE.set(d.id, w); } return w; }
  isClientSide() { return false; } m_5776_() { return false; }
  getServer() { return SERVER; } getLevel() { return this; } getRandom() { return RANDOM; } getRandomSource() { return RANDOM; }
  dimension() { return { key: this.d.id, location: () => this.d.id, equals: (o) => o && o.key === this.d.id }; }
  players() { try { return JList.of(this.d.getPlayers().map(JEntity.of)); } catch { return new JList(); } }
  getEntitiesOfClass(cls, box, pred) { return R.entitiesIn(this, cls, box, pred); }
  getEntities(ex, box, pred) { return R.entitiesIn(this, { cls: "net/minecraft/world/entity/Entity" }, box, pred); }
  getNearestPlayer(x, y, z, dist) { try { const p = this.d.getPlayers({ location: { x, y, z }, maxDistance: dist < 0 ? 1e6 : dist, closest: 1 })[0]; return JEntity.of(p); } catch { return null; } }
  getBlockState(p) { return R.blockAt(this, p); }
  setBlock(p, st, flags) { return R.setBlockAt(this, p, st); }
  setBlockAndUpdate(p, st) { return R.setBlockAt(this, p, st); }
  removeBlock(p) { return R.setBlockAt(this, p, BLOCK("minecraft:air")); }
  destroyBlock(p, drop) { try { const b = this.d.getBlock(p.toB()); if (b && !b.isAir) { this.d.runCommand(`setblock ${p.x_} ${p.y_} ${p.z_} air ${drop ? "destroy" : "replace"}`); return true; } } catch {} return false; }
  isEmptyBlock(p) { try { return !!this.d.getBlock(p.toB())?.isAir; } catch { return true; } }
  getHeight(type, x, z) { try { const t = this.d.getTopmostBlock({ x: Math.floor(x), z: Math.floor(z) }); return t ? t.location.y + 1 : this.d.heightRange.min; } catch { return 64; } }
  getBlockFloorHeight(p) { try { const b = this.d.getBlock(p.toB()); return !b || b.isAir || b.isLiquid ? 0 : (R.solid(BLOCK(b.typeId)) ? 1 : 0); } catch { return 0; } }
  getMaxLocalRawBrightness(p) { try { return this.d.getBlock(p.toB())?.getLightLevel?.() ?? 15; } catch { return 15; } }
  getMaxBuildHeight() { try { return this.d.heightRange.max; } catch { return 320; } } getMinBuildHeight() { try { return this.d.heightRange.min; } catch { return -64; } }
  canSeeSkyFromBelowWater(p) { try { const t = this.d.getTopmostBlock({ x: p.x_, z: p.z_ }); return !t || t.location.y < p.y_; } catch { return true; } }
  canSeeSky(p) { return this.canSeeSkyFromBelowWater(p); }
  isDay() { const t = world.getTimeOfDay(); return t < 12500 || t > 23500; } isNight() { return !this.isDay(); }
  getDayTime() { return world.getTimeOfDay(); } getGameTime() { return system.currentTick; }
  getLevelData() { const s = world.getDefaultSpawnLocation(); return { getXSpawn: () => s.x, getYSpawn: () => (s.y > 300 ? 64 : s.y), getZSpawn: () => s.z, isRaining: () => false, getGameTime: () => system.currentTick, getDayTime: () => world.getTimeOfDay() }; }
  getDifficulty() { try { return { id: world.getDifficulty() }; } catch { return { id: "normal" }; } }
  playSound(a, b, c, d, e, f, g) {   // (Player|null, BlockPos, sound, source, vol, pitch) | (Player|null, x, y, z, sound, source, vol, pitch)
    let pos, snd, vol, pitch;
    if (b instanceof Vec) { pos = b.getCenter(); snd = c; vol = e; pitch = f; } else { pos = V(b, c, d); snd = e; vol = g; pitch = arguments[7]; }
    R.sound(this, pos, snd, vol, pitch);
  }
  playLocalSound(x, y, z, snd, src, vol, pitch) { R.sound(this, V(x, y, z), snd, vol, pitch); }
  levelEvent(a, b, c, d) {}
  sendParticles(type, x, y, z, n, dx, dy, dz, sp) { R.particles(this, type, x, y, z, n, dx, dy, dz); return n; }
  addParticle(type, x, y, z) { R.particles(this, type, x, y, z, 1, 0, 0, 0); }
  addFreshEntity(e) { return R.spawnPending(this, e); }
  explode(src, x, y, z, power) { try { this.d.createExplosion({ x, y, z }, power, { breaksBlocks: true }); } catch {} return null; }
  clip(ctx) { return R.clip(this, ctx); }
  registryAccess() { return REGACCESS; } getStructureManager() { return STRUCTMAN; }
  updateNeighborsAt() {} blockUpdated() {} scheduleTick() {}
  isLoaded() { return true; } hasChunkAt() { return true; }
  getDataStorage() { return {}; }
}

// ------------------------------------------------------------------------------------------ blocks / items
const BLOCK = (id, states) => ({ __block: true, id, states: states ?? {} });
function bid(x) {
  const id = typeof x === "string" ? x : x?.id ?? "minecraft:air";
  if (id.startsWith("minecraft:")) { const b = CFG.blockJ2B?.[id.slice(10)]; if (b) return "minecraft:" + b; }
  return id;
}
const DIR = { DOWN: { n: "down", dx: 0, dy: -1, dz: 0 }, UP: { n: "up", dx: 0, dy: 1, dz: 0 }, NORTH: { n: "north", dx: 0, dy: 0, dz: -1 },
  SOUTH: { n: "south", dx: 0, dy: 0, dz: 1 }, WEST: { n: "west", dx: -1, dy: 0, dz: 0 }, EAST: { n: "east", dx: 1, dy: 0, dz: 0 } };
class Stack { constructor(id, n = 1, dmg = 0) { this.id = id; this.n = n; this.dmg = dmg; }
  getItem() { return { __item: true, id: this.id }; } getCount() { return this.n; } setCount(n) { this.n = n; R.syncStack(this); } shrink(k) { this.n -= k; R.syncStack(this); } grow(k) { this.n += k; R.syncStack(this); }
  isEmpty() { return !this.id || this.id === "minecraft:air" || this.n <= 0; } is(x) { return bid(x) === this.id; } copy() { return new Stack(this.id, this.n, this.dmg); }
  hurt(k) { this.dmg += k; R.syncStack(this); return false; } setDamageValue(d) { this.dmg = d; R.syncStack(this); } getDamageValue() { return this.dmg; }
  getOrCreateTag() { return new Tag(this, "item"); } getTag() { return new Tag(this, "item"); } hurtAndBreak(k) { this.hurt(k); }
  getDisplayName() { return this.id; } getHoverName() { return this.id; } setHoverName() {} enchant() {}
}
class Inv { constructor(p) { this.p = p; this.armor = JList.of(["feet", "legs", "chest", "head"].map((s) => R.handItem(p, s))); this.items = this.list(); this.selected = 0; }
  cont() { try { return this.p.e.getComponent("inventory").container; } catch { return null; } }
  list() { const c = this.cont(); const l = new JList(); if (!c) return l; for (let i = 0; i < c.size; i++) { const it = c.getItem(i); l.push(it ? new Stack(it.typeId, it.amount) : new Stack("minecraft:air", 0)); } return l; }
  contains(stack) { const id = bid(stack?.id ?? stack); return this.list().some((s) => s.id === id); }
  clearOrCountMatchingItems(pred, n) { const id = pred?.id ?? pred?.__item_id; try { this.p.e.runCommand(`clear @s ${id} 0 ${n < 0 ? 9999 : n}`); } catch {} return 0; }
  add(stack) { try { this.cont().addItem(new BItemStack(stack.id, Math.max(1, stack.n))); return true; } catch { return false; } }
  getSelected() { return R.handItem(this.p, "mainhand"); } setChanged() {} getContainerSize() { return this.cont()?.size ?? 36; } getItem(i) { return this.list()[i] ?? new Stack("minecraft:air", 0); }
  removeItem(i, n) {} setItem(i, s) { try { this.cont().setItem(i, s.isEmpty() ? undefined : new BItemStack(s.id, s.n)); } catch {} }
}
class Cooldowns { constructor(p) { this.p = p; } addCooldown(item, ticks) { R.cool.set(this.p.e.id + bid(item), system.currentTick + ticks); } isOnCooldown(item) { return (R.cool.get(this.p.e.id + bid(item)) ?? 0) > system.currentTick; } }
// MobAI: Java MoveControl (setWantedPosition / hasWanted). The GoalRunner (hooks.js) steers every mob that has a
// wanted position one step per tick, like Java's MoveControl.tick(); fliers get no gravity pull-down.
export class MoveCtl {
  static of(w) { let c = MOVES.get(w.e.id); if (!c) { c = new MoveCtl(w); MOVES.set(w.e.id, c); } return c; }
  constructor(w) { this.w = w; this.want = null; this.speed = 1; }
  setWantedPosition(x, y, z, sp) { this.want = { x: +x, y: +y, z: +z }; this.speed = +sp || 1; }
  hasWanted() { return !!this.want; } getWantedX() { return this.want?.x ?? this.w.getX(); } getWantedY() { return this.want?.y ?? this.w.getY(); }
  getWantedZ() { return this.want?.z ?? this.w.getZ(); } getSpeedModifier() { return this.speed; }
  step() {
    const e = this.w.e; if (!this.want) return;
    let l; try { l = e.location; } catch { MOVES.delete(e.id); return; }
    const dx = this.want.x - l.x, dy = this.want.y - l.y, dz = this.want.z - l.z, d = Math.hypot(dx, dy, dz);
    if (d < 0.5) { this.want = null; return; }
    let base = 0.25; try { base = e.getComponent("movement")?.currentValue ?? 0.25; } catch {}
    const fly = (() => { try { return !!e.getComponent("can_fly"); } catch { return false; } })();
    const v = Math.min(d, base * this.speed * (fly ? 1.0 : 0.6));
    let cur = { x: 0, y: 0, z: 0 }; try { cur = e.getVelocity(); } catch {}
    const want = { x: dx / d * v, y: fly ? dy / d * v : 0, z: dz / d * v };
    try { e.applyImpulse({ x: (want.x - cur.x) * 0.5, y: fly ? (want.y - cur.y) * 0.5 : 0, z: (want.z - cur.z) * 0.5 }); } catch {}
    try { e.setRotation({ x: 0, y: Math.atan2(-dx, dz) * 180 / Math.PI }); } catch {}
  }
}
export const MOVES = new Map();
const TARGETS = new Map();   // mob id -> entity it was told to target
class Nav { constructor(m) { this.m = m; } moveTo(a, b, c, sp) { if (a instanceof JEntity || a instanceof Vec) { R.moveTo(this.m, a, null, null, b); } else R.moveTo(this.m, a, b, c, sp); return true; } stop() { MoveCtl.of(this.m).want = null; } isDone() { return !MoveCtl.of(this.m).want; } createPath() { return null; } }

// ------------------------------------------------------------------------------------------ data: NBT, synced data, variables
const DP = (e) => e?.e ?? world;
function dpGet(holder, k) { try { const v = (holder === world ? world : holder).getDynamicProperty(k); return v; } catch { return undefined; } }
function dpSet(holder, k, v) { try { (holder === world ? world : holder).setDynamicProperty(k, v === null ? undefined : (typeof v === "boolean" || typeof v === "number" || typeof v === "string") ? v : JSON.stringify(v)); } catch {} }
class Tag {   // CompoundTag over dynamic properties (entities: persistent data; stacks: in memory)
  constructor(owner, ns) { this.o = owner; this.ns = ns; this.mem = owner instanceof Stack ? (owner.tag ??= {}) : null; }
  k(key) { return `${this.ns}:${key}`; }
  raw(key) { if (this.mem) return this.mem[key]; return dpGet(this.o.e, this.k(key)); }
  put(key, v) { if (this.mem) this.mem[key] = v; else dpSet(this.o.e, this.k(key), v); }
  getDouble(k) { return +(this.raw(k) ?? 0) || 0; } getInt(k) { return (+(this.raw(k) ?? 0)) | 0; } getFloat(k) { return this.getDouble(k); } getLong(k) { return this.getDouble(k); }
  getBoolean(k) { const v = this.raw(k); return v === true || v === 1 || v === "true"; } getString(k) { const v = this.raw(k); return v == null ? "" : String(v); }
  putDouble(k, v) { this.put(k, +v); } putInt(k, v) { this.put(k, v | 0); } putFloat(k, v) { this.put(k, +v); } putLong(k, v) { this.put(k, +v); }
  putBoolean(k, v) { this.put(k, !!v); } putString(k, v) { this.put(k, String(v)); } contains(k) { return this.raw(k) !== undefined; } remove(k) { this.put(k, null); }
}
class SynchedData {
  constructor(w) { this.w = w; }
  get(acc) { const n = acc?.name ?? "data"; if (n === "ANIMATION") return this.w.__fields.animationprocedure ?? "empty";
    const v = dpGet(this.w.e, "sd:" + n); return v === undefined ? (acc?.def ?? R.defaultFor(acc)) : v; }
  set(acc, v) { const n = acc?.name ?? "data";
    if (n === "ANIMATION") { this.w.__fields.animationprocedure = v; if (v && v !== "empty") { try { globalThis.gbTrigger?.(this.w.e, v); } catch {} } return; }
    dpSet(this.w.e, "sd:" + n, typeof v === "object" && v !== null ? JSON.stringify(v) : v);
    if (CFG.propSync?.[n]) { try { this.w.e.setProperty(CFG.propSync[n], v); } catch {} } }
  define(acc, v) {}
}
const VARS_CACHE = new Map();
class PlayerVars {      // MCreator PlayerVariables capability -> player dynamic properties "pv:<field>"
  constructor(w) { this.__pv = w; }
}
function mapVars() {     // MCreator MapVariables / WorldVariables -> world dynamic properties "mv:<field>"
  let o = VARS_CACHE.get("map");
  if (!o) { o = { __mv: true }; VARS_CACHE.set("map", o); const id = CFG.mapVarsInit; if (id) { try { const tmp = M.newObj ? null : null; } catch {} } }
  return o;
}

// ------------------------------------------------------------------------------------------ server / commands
const SERVER = {
  getPlayerList() { return { broadcastSystemMessage: (c) => { try { world.sendMessage(R.text(c)); } catch {} }, getPlayers: () => JList.of(world.getAllPlayers().map(JEntity.of)), getPlayerCount: () => world.getAllPlayers().length }; },
  getCommands() { return { performPrefixedCommand: (src, cmd) => R.command(src, cmd) }; }, m_129892_() { return this.getCommands(); },
  getLevel(key) { try { return JLevel.of(world.getDimension(R.dimId(key))); } catch { return null; } }, m_129880_(key) { return this.getLevel(key); },
  overworld() { return JLevel.of(world.getDimension("minecraft:overworld")); }, getStructureManager() { return STRUCTMAN; }, m_236738_() { return STRUCTMAN; },
  getTickCount() { return system.currentTick; }, isDedicatedServer() { return false; }, registryAccess() { return REGACCESS; },
};
const REGACCESS = { registryOrThrow: (k) => ({ getHolderOrThrow: (key) => ({ key: R.dimId(key) }), get: (key) => ({ key }) }), registry: () => new JOpt(null) };
const STRUCTMAN = { getOrCreate: (rl) => ({ __tpl: R.rl(rl) }), get: (rl) => new JOpt({ __tpl: R.rl(rl) }) };
const RANDOM = { nextInt(a, b) { return b === undefined ? Math.floor(Math.random() * a) : a + Math.floor(Math.random() * (b - a)); }, nextFloat() { return Math.random(); },
  nextDouble() { return Math.random(); }, nextBoolean() { return Math.random() < 0.5; }, nextGaussian() { let u = 0, v = 0; while (!u) u = Math.random(); while (!v) v = Math.random(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); },
  m_188503_(n) { return Math.floor(Math.random() * n); }, m_188501_() { return Math.random(); }, m_188500_() { return Math.random(); }, m_216339_(a, b) { return a + Math.floor(Math.random() * (b - a)); } };

// Java (1.20.1) -> Bedrock effect ids
const EFFECTS = { MOVEMENT_SPEED: "speed", MOVEMENT_SLOWDOWN: "slowness", DIG_SPEED: "haste", DIG_SLOWDOWN: "mining_fatigue", DAMAGE_BOOST: "strength",
  HEAL: "instant_health", HARM: "instant_damage", JUMP: "jump_boost", CONFUSION: "nausea", REGENERATION: "regeneration", DAMAGE_RESISTANCE: "resistance",
  FIRE_RESISTANCE: "fire_resistance", WATER_BREATHING: "water_breathing", INVISIBILITY: "invisibility", BLINDNESS: "blindness", NIGHT_VISION: "night_vision",
  HUNGER: "hunger", WEAKNESS: "weakness", POISON: "poison", WITHER: "wither", HEALTH_BOOST: "health_boost", ABSORPTION: "absorption", SATURATION: "saturation",
  GLOWING: "glowing", LEVITATION: "levitation", LUCK: "luck", UNLUCK: "bad_luck", SLOW_FALLING: "slow_falling", CONDUIT_POWER: "conduit_power",
  DOLPHINS_GRACE: "conduit_power", BAD_OMEN: "bad_omen", HERO_OF_THE_VILLAGE: "village_hero", DARKNESS: "darkness" };
const MODFX = new Map();   // entity id -> {effect: expiryTick}

export const R = {
  cool: new Map(),
  mapVars(...a) { return mapVars(); },
  num(v) { return typeof v === "boolean" ? (v ? 1 : 0) : (v ?? 0); },
  same(a, b) { if (a === b) return true; if (a && b && a.__block && b.__block) return a.id === b.id; if (a && b && a.__item && b.__item) return a.id === b.id; if (a && b && a.key && b.key) return a.key === b.key; if (a instanceof JEntity && b instanceof JEntity) return a.e.id === b.e.id; if (a && b && a.__fx && b.__fx) return a.id === b.id; return false; },
  equals(a, b) { if (typeof a === "string" || typeof b === "string") return String(a) === String(b); return R.same(a, b) || (a?.key != null && a.key === (b?.key ?? b)); },
  idiv(a, b, o) { if (b === 0) throw new Error("/ by zero"); return o === "/" ? Math.trunc(a / b) | 0 : (a % b) | 0; },
  ldiv(a, b, o) { return o === "/" ? Math.trunc(a / b) : a % b; },
  toInt(v) { if (Number.isNaN(v)) return 0; return v >= 2147483647 ? 2147483647 : v <= -2147483648 ? -2147483648 : Math.trunc(v); },
  cls(name) { return { cls: name.replace(/\./g, "/") }; },
  newObj(cls) { return { __cls: cls }; },
  concat(recipe, args) { let i = 0; return recipe.replace(/\u0001/g, () => R.str(args[i++])); },
  str(v) { if (v == null) return "null"; if (typeof v === "number") return Number.isInteger(v) ? String(v) : String(v); if (v instanceof JEntity) return v.getDisplayName(); if (v.key) return v.key; if (v.id) return v.id; return String(v); },
  lambda(iname, f) { const o = { __lambda: f }; o[iname] = f; o.test = o.test ?? f; o.apply = o.apply ?? f; o.accept = o.accept ?? f; o.get = o.get ?? f; o.run = o.run ?? f; o.applyAsDouble = o.applyAsDouble ?? f; o.applyAsInt = o.applyAsInt ?? f; o.compare = o.compare ?? f; return o; },
  lambdaApi(owner, name, captured) { return R.lambda("apply", (...xs) => R.call(xs[0], name, xs.slice(1), owner)); },
  lam(f, ...xs) { if (!f) return null; if (typeof f === "function") return f(...xs); if (f.__lambda) return f.__lambda(...xs); return null; },
  text(c) { if (c == null) return ""; if (typeof c === "string") return c; return c.__text ?? String(c); },
  rl(x) { if (x == null) return ""; if (typeof x === "string") return x.includes(":") ? x : "minecraft:" + x; return x.rl ?? x.key ?? x.id ?? String(x); },
  dimId(key) { const k = key?.key ?? key; return typeof k === "string" ? k : "minecraft:overworld"; },
  defaultFor(acc) { return acc?.ser === "BOOLEAN" ? false : acc?.ser === "STRING" ? "" : 0; },
  gamemode(w) { try { return String(w.e.getGameMode()).toLowerCase(); } catch { return "survival"; } },
  flag(w, k, v) { try { w.e.setDynamicProperty("jb:" + k, v); } catch {} },
  // ---- fields
  gfield(o, f) {
    if (o == null) return null;
    if (o.__pv) { const v = dpGet(o.__pv.e, "pv:" + f); return v === undefined ? (CFG.pvDefaults?.[f] ?? 0) : v; }
    if (o.__mv) { const v = dpGet(world, "mv:" + f); return v === undefined ? (CFG.mvDefaults?.[f] ?? 0) : v; }
    if (o instanceof JEntity) {
      if (f === "connection") return { send() {}, teleport: (x, y, z, yaw, pitch) => o.teleportTo(x, y, z) };
      if (f === "server") return SERVER; if (f === "gameMode") return { getGameModeForPlayer: () => ({ id: R.gamemode(o) }) };
      if (f === "fallDistance") return 0; if (f === "noPhysics") return false; if (f === "level") return o.level();
      if (f === "yRotO" || f === "yBodyRotO" || f === "yHeadRotO") return o.getYRot(); if (f === "xRotO") return o.getXRot();
      if (f === "inventoryMenu") return { getCraftSlots: () => ({ getContainerSize: () => 4, getItem: () => new Stack("minecraft:air", 0) }) };
      if (f === "player") return o;
      if (f === "moveControl") return MoveCtl.of(o); if (f === "navigation") return o.getNavigation();
      return o.__fields[f] ?? null;
    }
    if (o instanceof Vec && (f === "x" || f === "y" || f === "z")) return o[f + "_"];   // Vec3.x/y/z fields
    if (o instanceof Inv) return o[f];
    if (o.__ev) return o[f];
    if (f in o) return o[f];
    return null;
  },
  pfield(o, f, v) {
    if (o == null) return;
    if (o.__pv) { dpSet(o.__pv.e, "pv:" + f, v); return; }
    if (o.__mv) { dpSet(world, "mv:" + f, v); return; }
    if (o instanceof JEntity) { o.__fields[f] = v; if (f === "fallDistance" && v === 0) {} return; }
    o[f] = v;
  },
  // ---- statics / registries
  registryField(cls, f) {
    const c = cls.split("/").pop();
    if (c === "Blocks") return BLOCK("minecraft:" + f.toLowerCase());
    if (c === "Items") return { __item: true, id: "minecraft:" + f.toLowerCase() };
    if (c === "MobEffects") return { __fx: true, id: EFFECTS[f] ?? f.toLowerCase() };
    if (c === "Direction") return DIR[f];
    if (c === "SoundSource" || c === "Rotation" || c === "Mirror" || c === "MobSpawnType" || c === "GameType" || c === "Difficulty" || c === "EquipmentSlot" || c === "InteractionHand" || c === "HitResult$Type" || c === "TickEvent$Phase" || c === "Level$ExplosionInteraction" || c === "ClipContext$Block" || c === "ClipContext$Fluid" || c === "EntityAnchorArgument$Anchor" || c === "Heightmap$Types")
      return { key: f, id: f.toLowerCase(), slot: { FEET: "feet", LEGS: "legs", CHEST: "chest", HEAD: "head", MAINHAND: "mainhand", OFFHAND: "offhand" }[f], hand: { MAIN_HAND: "mainhand", OFF_HAND: "offhand" }[f] };
    if (c === "Level") return { key: { OVERWORLD: "minecraft:overworld", NETHER: "minecraft:nether", END: "minecraft:the_end" }[f] ?? f };
    if (c === "DamageTypes") return { key: f.toLowerCase(), dmg: true };
    if (c === "ParticleTypes") return { particle: f.toLowerCase() };
    if (c === "Registries" || c === "ForgeRegistries") return { registry: f };
    if (c === "EntityType") return { etype: "minecraft:" + f.toLowerCase() };
    if (c === "ItemStack" && f === "EMPTY") return new Stack("minecraft:air", 0);
    if (c === "BlockPos" && f === "ZERO") return V(0, 0, 0); if (c === "Vec3" && f === "ZERO") return V(0, 0, 0); if (c === "Vec2") return { x: 0, y: 0 };
    if (c === "CommandSource" && f === "NULL") return { nullsrc: true };
    if (c === "EntityDataSerializers") return { key: f };
    if (c === "Locale") return {};
    if (c === "InteractionResult") return { key: f };
    if (c === "PacketDistributor") return { with() { return {}; }, noArg() { return {}; } };
    if (c === "ClientboundGameEventPacket") return { key: f };
    // the mod's own registries: <Mod>Blocks.FIELD.get() etc.
    const reg = CFG.registries?.[c]; if (reg) { const name = f.toLowerCase(); return { __ro: true, kind: reg, id: `${NS}:${name}`, get() { return R.regValue(reg, `${NS}:${name}`); } }; }
    if (CFG.accessorClasses?.includes(cls)) return undefined;
    return undefined;
  },
  regValue(kind, id) {
    if (kind === "block") return BLOCK(id); if (kind === "item") return { __item: true, id }; if (kind === "entity") return { etype: id };
    if (kind === "effect") return { __fx: true, id, mod: true }; if (kind === "sound") return { sound: id }; if (kind === "particle") return { particle: id };
    return { id };
  },
  sget(cls, f) { const v = R.registryField(cls, f); if (v === undefined) warn("static " + cls + "." + f); return v ?? { key: f }; },
  // ---- object construction (new + <init>)
  swap(S, L, o, v) { for (let i = 0; i < S.length; i++) if (S[i] === o) S[i] = v; for (let i = 0; i < L.length; i++) if (L[i] === o) L[i] = v; },
  make(cls, desc, a) {
    const c = cls.split("/").pop(); let v = null;
    switch (c) {
      case "Vec3": case "BlockPos": case "Vec3i": v = c === "Vec3" ? V(a[0], a[1], a[2]) : bp(a[0], a[1], a[2]); break;
      case "AABB": v = a.length === 2 ? new AABB(a[0].x_, a[0].y_, a[0].z_, a[1].x_, a[1].y_, a[1].z_) : new AABB(...a); break;
      case "ResourceLocation": v = { rl: a.length === 2 ? `${a[0]}:${a[1]}` : (String(a[0]).includes(":") ? String(a[0]) : "minecraft:" + a[0]) }; v.toString = () => v.rl; break;
      case "MobEffectInstance": v = { __fxi: true, effect: a[0], dur: a[1] ?? 0, amp: a[2] ?? 0, visible: a.length > 4 ? !!a[4] : true }; break;
      case "ItemStack": v = new Stack(bid(a[0]?.id ?? a[0]), a[1] ?? 1); break;
      case "DamageSource": v = { cause: R.damageCause(a[0]?.key ?? a[0]), attacker: a[1] instanceof JEntity ? a[1] : null }; break;
      case "CommandSourceStack": v = { __src: true, pos: a[1], level: a[3], entity: a[8] instanceof JEntity ? a[8] : null }; break;
      case "ItemEntity": v = { __spawnItem: true, level: a[0], pos: V(a[1], a[2], a[3]), stack: a[4] }; break;
      case "ArrayList": case "LinkedList": v = new JList(); if (a[0] && a[0].length !== undefined) for (const x of a[0]) v.push(x); break;
      case "HashMap": v = new Map(); break; case "StringBuilder": v = { __sb: String(a[0] ?? "") }; break;
      case "ClipContext": v = { from: a[0], to: a[1], entity: a[4] }; break;
      case "StructurePlaceSettings": v = { rot: "None", mirror: "None", setRotation(r) { this.rot = r?.key ?? "NONE"; return this; }, setMirror(m) { this.mirror = m?.key ?? "NONE"; return this; }, setIgnoreEntities() { return this; } }; break;
      case "TextComponent": v = { __text: String(a[0]) }; break;
      case "HashSet": case "ConcurrentLinkedQueue": case "ArrayDeque": v = new JList(); break;
      case "AbstractMap$SimpleEntry": v = { k: a[0], v: a[1] }; break;
      case "CapabilityToken": v = { cap: true }; break;
      case "Object": v = {}; break;
      case "ForgeConfigSpec$Builder": v = CONFIG_BUILDER; break;
      case "ClientboundGameEventPacket": case "ClientboundPlayerAbilitiesPacket": case "ClientboundUpdateMobEffectPacket": case "ClientboundLevelEventPacket": v = { packet: c }; break;
      case "Vec2": v = { x: a[0], y: a[1] }; break;
      default: warn("new " + cls); v = {};
    }
    if (v instanceof Map) v = { __map: v };
    return v;
  },
  damageCause(k) {
    const m = { generic: "entityAttack", fell_out_of_world: "void", fly_into_wall: "flyIntoWall", explosion: "entityExplosion", starve: "starve", drown: "drowning", magic: "magic", wither: "wither", in_fire: "fire", on_fire: "fireTick", lava: "lava", fall: "fall" };
    const key = String(k?.key ?? k ?? "generic").split(":").pop();
    return m[key] ?? "entityAttack";
  },
  // ---- static calls
  st(owner, name, desc, a) {
    const c = owner.split("/").pop();
    const k = c + "." + name;
    switch (k) {
      case "BlockPos.containing": return a.length === 1 ? bp(a[0].x_, a[0].y_, a[0].z_) : bp(a[0], a[1], a[2]);
      case "AABB.ofSize": return new AABB(a[0].x_ - a[1] / 2, a[0].y_ - a[2] / 2, a[0].z_ - a[3] / 2, a[0].x_ + a[1] / 2, a[0].y_ + a[2] / 2, a[0].z_ + a[3] / 2);
      case "RandomSource.create": return RANDOM;
      case "Mth.nextInt": return a[1] + Math.floor(Math.random() * (a[2] - a[1] + 1));
      case "Mth.nextDouble": return a[1] + Math.random() * (a[2] - a[1]);
      case "Mth.nextFloat": return a[1] + Math.random() * (a[2] - a[1]);
      case "Mth.floor": return Math.floor(a[0]); case "Mth.sqrt": return Math.sqrt(a[0]); case "Mth.abs": return Math.abs(a[0]);
      case "Mth.clamp": return Math.min(Math.max(a[0], a[1]), a[2]); case "Mth.sin": return Math.sin(a[0]); case "Mth.cos": return Math.cos(a[0]);
      case "Mth.wrapDegrees": { let d = a[0] % 360; if (d >= 180) d -= 360; if (d < -180) d += 360; return d; }
      case "Math.random": return Math.random(); case "Math.floor": return Math.floor(a[0]); case "Math.ceil": return Math.ceil(a[0]); case "Math.abs": return Math.abs(a[0]);
      case "Math.sqrt": return Math.sqrt(a[0]); case "Math.pow": return Math.pow(a[0], a[1]); case "Math.sin": return Math.sin(a[0]); case "Math.cos": return Math.cos(a[0]);
      case "Math.atan": return Math.atan(a[0]); case "Math.atan2": return Math.atan2(a[0], a[1]); case "Math.round": return Math.round(a[0]); case "Math.min": return Math.min(a[0], a[1]);
      case "Math.max": return Math.max(a[0], a[1]); case "Math.toRadians": return a[0] * Math.PI / 180; case "Math.toDegrees": return a[0] * 180 / Math.PI; case "Math.signum": return Math.sign(a[0]);
      case "Component.literal": case "Component.translatable": return { __text: String(a[0] ?? "") };
      case "Comparator.comparingDouble": return { keyFn: a[0], compare: (x, y) => R.lam(a[0], x) - R.lam(a[0], y) };
      case "Comparator.comparingInt": return { compare: (x, y) => R.lam(a[0], x) - R.lam(a[0], y) };
      case "Integer.valueOf": case "Double.valueOf": case "Float.valueOf": case "Long.valueOf": return a[0];
      case "Boolean.valueOf": return !!a[0]; case "String.valueOf": return R.str(a[0]);
      case "Integer.parseInt": return parseInt(a[0]) | 0; case "Double.parseDouble": return parseFloat(a[0]);
      case "Objects.equals": return R.equals(a[0], a[1]); case "Objects.requireNonNull": return a[0];
      case "ResourceKey.create": return { key: R.rl(a[1]) };
      case "BlockTags.create": case "ItemTags.create": case "TagKey.create": return { tag: R.rl(a[a.length - 1]) };
      case "SynchedEntityData.defineId": return { __acc: true, name: null, ser: a[1]?.key ?? "STRING" };
      case "Collections.shuffle": { const l = a[0]; for (let i = l.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [l[i], l[j]] = [l[j], l[i]]; } return; }
      case "List.of": return JList.of(a); case "Arrays.asList": return JList.of(a[0] ?? []);
      case "Rotation.getRandom": return { key: ["NONE", "CLOCKWISE_90", "CLOCKWISE_180", "COUNTERCLOCKWISE_90"][Math.floor(Math.random() * 4)] };
      case "Minecraft.getInstance": return CLIENT;
      case "Vec3.atBottomCenterOf": return V(a[0].x_ + 0.5, a[0].y_, a[0].z_ + 0.5); case "Vec3.atCenterOf": return V(a[0].x_ + 0.5, a[0].y_ + 0.5, a[0].z_ + 0.5);
      case "LogManager.getLogger": return { info() {}, warn() {}, error() {}, debug() {} };
      case "NetworkRegistry.newSimpleChannel": return { registerMessage() {}, send() {}, sendToServer() {} };
      case "InteractionResult.sidedSuccess": return { key: "SUCCESS" };
      case "CapabilityManager.get": return { cap: true };
      case "System.currentTimeMillis": return Date.now();
      case "Direction.getRandom": return Object.values(DIR)[Math.floor(Math.random() * 6)];
      case "ItemStack.of": return new Stack("minecraft:air", 0);
      case "StringArgumentType.getString": return "";
      case "Entity.getStringUUID": return a[0]?.e?.id;
      case "Item.byBlock": return { __item: true, id: bid(a[0]) };
      case "Block.byItem": return BLOCK(a[0]?.id);
      case "Block.getId": return 0;
      case "BlockStateParser.parseForBlock": return BLOCK("minecraft:air");
      case "ForgeConfigSpec$Builder.<init>": return CONFIG_BUILDER;
    }
    if (c.endsWith("ModVariables$MapVariables") && name === "get") return mapVars();
    if (c.endsWith("ModVariables$WorldVariables") && name === "get") return mapVars();
    if (c === "ForgeRegistries" || c === "IForgeRegistry") return null;
    warn("static " + k); return null;
  },
  superCall(o, name, a) { if (o?.__goal && (name === "canUse" || name === "canContinueToUse")) return true; return null; },
  // ---- virtual calls on non-mod objects
  call(o, name, a, owner) {
    if (o == null) return null;
    if (typeof o === "string") return R.strcall(o, name, a);
    if (typeof o === "number" || typeof o === "boolean") {
      if (name === "intValue" || name === "doubleValue" || name === "floatValue" || name === "longValue") return +o;
      if (name === "booleanValue") return !!o; if (name === "equals") return o === a[0]; if (name === "toString") return String(o); return o;
    }
    if (typeof o[name] === "function" && !(o.__lambda && name in o === false)) {
      try { return o[name](...a); } catch (e) { warn(`${name} threw ${e}`); return null; }
    }
    // generic objects
    switch (name) {
      case "get": if (o.__ro) return o.get(); if (o.__map) return o.__map.get(a[0]) ?? null; if (o.__cfg) return o.v; if (o instanceof JOpt) return o.v; break;
      case "getValue": if (o.registry === "SOUND_EVENTS") return { sound: R.rl(a[0]) }; if (o.registry === "BLOCKS") return BLOCK(R.rl(a[0])); if (o.registry === "ITEMS") return { __item: true, id: R.rl(a[0]) }; if (o.registry === "ENTITY_TYPES") return { etype: R.rl(a[0]) }; if (o.__state) return o.states?.[a[0]?.name]; break;
      case "getKey": if (o.registry) return { rl: a[0]?.id ?? a[0]?.etype ?? "", toString() { return this.rl; } }; break;
      case "put": if (o.__map) { o.__map.set(a[0], a[1]); return null; } break;
      case "containsKey": if (o.__map) return o.__map.has(a[0]); break;
      case "spawn": if (o.etype) return R.spawn(a[0], o.etype, a.find((x) => x instanceof Vec) ?? V(0, 64, 0)); break;   // (level, pos, type) | (level, stack, player, pos, type, b, b)
      case "create": if (o.etype) return null; break;
      case "getBlock": if (o.__block) return o; break;
      case "defaultBlockState": if (o.__block) return BLOCK(o.id); break;
      case "is": if (o.__block) return a[0]?.tag ? R.blockTag(o, a[0].tag) : bid(a[0]) === o.id; if (o.__item) return bid(a[0]) === o.id; break;
      case "canOcclude": if (o.__block) return R.solid(o); break;
      case "isSolidRender": case "isSolid": case "isFaceSturdy": case "isCollisionShapeFullBlock": if (o.__block) return R.solid(o); break;
      case "getDestroySpeed": if (o.__block) return o.id === "minecraft:bedrock" ? -1 : 1.5; break;
      case "getLightBlock": if (o.__block) return R.solid(o) ? 15 : 0; break;
      case "isAir": if (o.__block) return /air$/.test(o.id); break;
      case "getStateDefinition": if (o.__block) return { getProperty: (n) => ({ name: n }) }; break;
      case "setValue": if (o.__block) return BLOCK(o.id, { ...o.states, [a[0]?.name]: a[1] }); break;
      case "asItem": return { __item: true, id: o.id };
      case "equals": return R.equals(o, a[0]);
      case "location": if (o.key) return { rl: o.key, toString() { return this.rl; } }; break;
      case "toString": return R.str(o);
      case "getString": if (o.__text !== undefined) return o.__text; break;
      case "getDuration": if (o.__fxi) return o.dur; break;
      case "getAmplifier": if (o.__fxi) return o.amp; break;
      case "getId": if (o.__acc) return 0; break;
      case "getType": if (o.hit) return { key: o.hit }; break;
      case "getBlockPos": if (o.pos) return o.pos; break;
      case "getLocation": if (o.posv) return o.posv; break;
      case "placeInWorld": if (o.__tpl) return R.placeStructure(a[0], o.__tpl, a[1], a[3]); break;
      case "getEntity": case "getPlayer": if (o.__ev) return o.entity ?? o.player; if (o.cause !== undefined) return o.attacker ?? null; break;
      case "getFrom": if (o.__ev) return { key: o.from }; break; case "getTo": if (o.__ev) return { key: o.to }; break;
      case "getHand": case "getUsedItemHand": return { key: "MAIN_HAND", hand: "mainhand" };
      case "computeIfAbsent": return mapVars();
      case "getKey": if (o.k !== undefined) return o.k; break; case "getValue": if (o.k !== undefined) return o.v; break;
      case "canChangeDimensions": return true;
      case "info": case "debug": case "error": case "registerMessage": return null;
      case "getLocation": if (o.posv) return o.posv; if (o.pos) return o.pos; break;
      case "getDirection": if (o.pos) return DIR.UP; break;
      case "getLevel": if (o.__ev) return o.level; break;
      case "getPos": if (o.__ev) return o.pos; break;
      case "getState": if (o.__ev) return o.state; break;
      case "getSource": if (o.__ev) return o.source; break;
      case "getRawText": case "getMessage": if (o.__ev) return o.text; break;
      case "isCancelable": return !!o.__ev; case "setCanceled": if (o.__ev) { o.canceled = !!a[0]; return; } break;
      case "withSuppressedOutput": case "withPermission": case "withEntity": return o.__src ? Object.assign(o, name === "withEntity" ? { entity: a[0] } : {}) : o;
      case "getCommands": return SERVER.getCommands();
      case "setPickUpDelay": return;
      case "getGameModeForPlayer": return { id: "survival" };
      case "append": if (o.__sb !== undefined) { o.__sb += R.str(a[0]); return o; } break;
      case "hashCode": return 0;
      case "setDirty": case "syncData": case "syncPlayerVariables": case "updateSwingTime": case "refreshDimensions": case "markDirty": return null;
      case "getDirectEntity": if (o.cause !== undefined) return o.attacker ?? null; break;
      case "add": if (Array.isArray(o)) { o.push(a[0]); return true; } break;
    }
    if (o.etype && name === "is") return o.etype === (a[0]?.etype ?? a[0]);
    if (o.cause !== undefined && name === "is") return String(a[0]?.key ?? a[0] ?? "").toLowerCase().includes(String(o.cause).toLowerCase());
    if (o.__cfg && name === "get") return o.v;
    warn(`call ${(owner || "?").split("/").pop()}.${name}`);
    return null;
  },
  strcall(s, name, a) {
    switch (name) {
      case "equals": return s === R.str(a[0]); case "equalsIgnoreCase": return s.toLowerCase() === R.str(a[0]).toLowerCase();
      case "contains": return s.includes(R.str(a[0])); case "startsWith": return s.startsWith(a[0]); case "endsWith": return s.endsWith(a[0]);
      case "toLowerCase": return s.toLowerCase(); case "toUpperCase": return s.toUpperCase(); case "length": return s.length; case "isEmpty": return s.length === 0;
      case "substring": return s.substring(a[0], a[1]); case "replace": return s.split(R.str(a[0])).join(R.str(a[1])); case "trim": return s.trim();
      case "indexOf": return s.indexOf(a[0]); case "charAt": return s.charCodeAt(a[0]); case "split": return JList.of(s.split(new RegExp(a[0])));
      case "hashCode": { let h = 0; for (let i = 0; i < s.length; i++) h = (Math.imul(31, h) + s.charCodeAt(i)) | 0; return h; }
      case "toString": case "intern": case "getString": case "getContents": case "plainCopy": case "copy": return s; case "matches": return new RegExp(`^${a[0]}$`).test(s);
    }
    warn("String." + name); return null;
  },
  instOf(o, cls) {
    if (o == null) return false;
    const c = cls.split("/").pop();
    if (o instanceof JEntity) {
      const t = o.e.typeId;
      if (c === "Player" || c === "ServerPlayer" || c === "LocalPlayer" || c === "AbstractClientPlayer") return t === "minecraft:player";
      if (c === "Entity") return true;
      if (c === "LivingEntity" || c === "Mob" || c === "PathfinderMob" || c === "Monster") { try { return !!o.e.getComponent("health") && (c === "LivingEntity" || t !== "minecraft:player"); } catch { return false; } }
      if (c === "ItemEntity") return t === "minecraft:item";
      if (c === "TamableAnimal") return false;
      const mc = o.modClass(); if (mc) { let x = mc, n = 0; while (x && n++ < 10) { if (x === cls) return true; x = CLS.get(x)?.sup; } }
      if (CFG.classToType?.[cls]) return CFG.classToType[cls] === t;
      if (cls.startsWith("net/minecraft/")) {      // vanilla class: abstract groups by family, concrete ones by snake-cased id
        if (c === "Animal" || c === "AgeableMob") { try { return t !== "minecraft:player" && !!o.e.getComponent("health") && !o.e.matches({ families: ["monster"] }); } catch { return false; } }
        if (c === "WaterAnimal" || c === "AbstractFish") { try { return o.e.matches({ families: ["fish"] }) || o.e.matches({ families: ["squid"] }); } catch { return false; } }
        if (c === "AbstractVillager") return t === "minecraft:villager_v2" || t === "minecraft:wandering_trader";
        if (c === "Villager") return t === "minecraft:villager_v2";
        return t === "minecraft:" + c.replace(/([a-z0-9])([A-Z])/g, "$1_$2").toLowerCase();
      }
      return false;
    }
    if (o.__cls) { let x = o.__cls, n = 0; while (x && n++ < 10) { if (x === cls) return true; x = CLS.get(x)?.sup; } return false; }
    if (c === "LevelAccessor" || c === "Level" || c === "ServerLevel" || c === "ServerLevelAccessor" || c === "WorldGenLevel") return o instanceof JLevel;
    if (c === "LivingEntity") return false;
    return false;
  },
  // ---- world helpers
  entitiesIn(lv, cls, box, pred) {
    const out = new JList();
    let list = [];
    try {
      const opt = { location: box.min.toB(), volume: { x: box.max.x_ - box.min.x_, y: box.max.y_ - box.min.y_, z: box.max.z_ - box.min.z_ } };
      const c = cls?.cls?.split("/").pop();
      if (c === "Player" || c === "ServerPlayer") opt.type = "minecraft:player";
      else if (CFG.classToType?.[cls?.cls]) opt.type = CFG.classToType[cls.cls];
      else if (c === "ItemEntity") opt.type = "minecraft:item";
      list = lv.d.getEntities(opt);
    } catch { return out; }
    const c = cls?.cls?.split("/").pop();
    for (const e of list) {
      const w = JEntity.of(e);
      if (c !== "Entity" && cls?.cls && !R.instOf(w, cls.cls)) continue;   // typed query: never hand back unrelated entities (or the player)
      if (pred && !R.lam(pred, w)) continue;
      out.push(w);
    }
    return out;
  },
  blockAt(lv, p) { try { const b = lv.d.getBlock(p.toB()); return b ? BLOCK(b.typeId, b.permutation.getAllStates()) : BLOCK("minecraft:air"); } catch { return BLOCK("minecraft:air"); } },
  setBlockAt(lv, p, st) {
    try { const id = CFG.blockRename?.[bid(st)] ?? bid(st); lv.d.getBlock(p.toB())?.setPermutation(BlockPermutation.resolve(id)); return true; }
    catch { try { lv.d.runCommand(`setblock ${p.x_} ${p.y_} ${p.z_} ${bid(st)}`); return true; } catch { return false; } }
  },
  solid(st) { return !/air$|water|lava|grass$|short_grass|flower|torch|sapling|vine|fern|bush|snow_layer|carpet|button|lever|rail|sign|banner/.test(st.id); },
  blockTag(st, tag) { return false; },
  sound(lv, pos, snd, vol = 1, pitch = 1) {
    const id = R.soundId(snd); if (!id) return;
    try { lv.d.playSound(id, pos.toB(), { volume: Math.max(0.01, vol ?? 1), pitch: pitch ?? 1 }); } catch {}
  },
  soundId(snd) { const rl = R.rl(snd?.sound ?? snd); if (!rl) return null; const [ns, path] = rl.split(":"); if (ns === MODID) return `${NS}.${path}`; return CFG.vanillaSounds?.[path] ?? path; },
  particles(lv, type, x, y, z, n, dx, dy, dz) {
    const id = CFG.particles?.[type?.particle] ?? (CFG.modParticles?.includes(type?.particle) ? type.particle : "minecraft:basic_smoke_particle");
    for (let i = 0; i < Math.min(n ?? 1, 24); i++) { try { lv.d.spawnParticle(id, { x: x + (Math.random() - 0.5) * 2 * (dx ?? 0), y: y + (Math.random() - 0.5) * 2 * (dy ?? 0), z: z + (Math.random() - 0.5) * 2 * (dz ?? 0) }); } catch {} }
  },
  spawn(lv, etype, pos) {
    try { const id = etype.etype ?? etype; const e = lv.d.spawnEntity(id, { x: pos.x_ + 0.5, y: pos.y_, z: pos.z_ + 0.5 }); return JEntity.of(e); } catch (e) { warn("spawn " + etype?.etype + " " + e); return null; }
  },
  spawnPending(lv, e) {
    if (e?.__spawnItem) { try { lv.d.spawnItem(new BItemStack(e.stack.id, Math.max(1, e.stack.n)), e.pos.toB()); return true; } catch { return false; } }
    if (e instanceof JEntity) return true;
    return false;
  },
  placeStructure(lv, tpl, pos, settings) {
    const id = CFG.structures?.[tpl] ?? tpl.replace(`${MODID}:`, `${NS}:`);
    try { world.structureManager.place(id, lv.d ?? lv, pos.toB(), { rotation: { NONE: "None", CLOCKWISE_90: "Rotate90", CLOCKWISE_180: "Rotate180", COUNTERCLOCKWISE_90: "Rotate270" }[settings?.rot ?? "NONE"] ?? "None" }); return true; }
    catch (e) { warn("structure " + id + " " + e); return false; }
  },
  clip(lv, ctx) {
    try {
      const f = ctx.from, t = ctx.to; const dir = { x: t.x_ - f.x_, y: t.y_ - f.y_, z: t.z_ - f.z_ }; const len = Math.hypot(dir.x, dir.y, dir.z) || 1;
      const hit = lv.d.getBlockFromRay(f.toB(), { x: dir.x / len, y: dir.y / len, z: dir.z / len }, { maxDistance: len, includeLiquidBlocks: false, includePassableBlocks: false });
      if (hit) { const b = hit.block.location; return { hit: "BLOCK", pos: V(b.x, b.y, b.z), posv: V(b.x + hit.faceLocation.x, b.y + hit.faceLocation.y, b.z + hit.faceLocation.z), getType: () => ({ key: "BLOCK" }), getBlockPos() { return this.pos; }, getLocation() { return this.posv; } }; }
    } catch {}
    return { hit: "MISS", pos: ctx.to, posv: ctx.to, getType: () => ({ key: "MISS" }), getBlockPos() { return bp(ctx.to.x_, ctx.to.y_, ctx.to.z_); }, getLocation() { return ctx.to; } };
  },
  handItem(w, slot) {
    try {
      const eq = w.e.getComponent("equippable");
      const s = { mainhand: "Mainhand", offhand: "Offhand", head: "Head", chest: "Chest", legs: "Legs", feet: "Feet" }[slot] ?? "Mainhand";
      const it = eq?.getEquipment(s); const st = new Stack(it?.typeId ?? "minecraft:air", it?.amount ?? 0); st.owner = w; st.slot = s; return st;
    } catch { return new Stack("minecraft:air", 0); }
  },
  setSlot(w, slot, stack) {
    try { const s = { mainhand: "Mainhand", offhand: "Offhand", head: "Head", chest: "Chest", legs: "Legs", feet: "Feet" }[slot] ?? "Mainhand";
      w.e.getComponent("equippable")?.setEquipment(s, stack && !stack.isEmpty() ? new BItemStack(stack.id, Math.max(1, stack.n)) : undefined); } catch {}
  },
  syncStack(st) {
    if (!st.owner || !st.slot) return;
    try { const eq = st.owner.e.getComponent("equippable"); if (st.n <= 0) { eq.setEquipment(st.slot, undefined); return; }
      const it = new BItemStack(st.id, st.n); const d = it.getComponent("durability"); if (d) { if (st.dmg >= d.maxDurability) { eq.setEquipment(st.slot, undefined); return; } d.damage = Math.min(st.dmg, d.maxDurability); }
      eq.setEquipment(st.slot, it); } catch {}
  },
  setTarget(w, t) {
    // MobAI: Bedrock has no setTarget. Every converted mob has a nearest_attackable_target entry for entities tagged
    // gbt_<its family>; tag the new target, untag the old one. Nothing is hurt (the old zero-damage hit fired the
    // mod's own hurt procedures).
    try {
      const tag = "gbt_" + w.e.typeId.replace(":", "_");
      const prev = TARGETS.get(w.e.id);
      if (prev && (!t || prev !== t.e)) { try { prev.removeTag(tag); } catch {} TARGETS.delete(w.e.id); }
      if (t && t.e && t.e !== w.e) { t.e.addTag(tag); TARGETS.set(w.e.id, t.e); }
    } catch {}
  },
  moveTo(w, a, b, c, sp) {
    // Java PathNavigation.moveTo: Bedrock scripts can't drive the pathfinder, so the GoalRunner steers toward the point
    try { const to = a instanceof Vec ? a.toB() : a instanceof JEntity ? a.loc() : { x: a, y: b, z: c }; MoveCtl.of(w).setWantedPosition(to.x, to.y, to.z, sp ?? 1); } catch {}
  },
  // ---- effects (vanilla -> Bedrock effects; the mod's own effects -> timed flags + their tick procedures)
  fxId(eff) { return eff?.__ro ? eff.id : eff?.id; },
  applyEffect(w, inst) {
    const eff = inst?.effect; if (!eff) return false; const id = R.fxId(eff);
    if (eff.mod || eff.__ro) { let m = MODFX.get(w.e.id); if (!m) MODFX.set(w.e.id, m = {}); m[id] = { until: system.currentTick + (inst.dur ?? 0), amp: inst.amp ?? 0 }; try { w.e.setDynamicProperty("fx:" + id, system.currentTick + (inst.dur ?? 0)); } catch {} return true; }
    try { return w.e.addEffect(id, Math.max(1, inst.dur), { amplifier: Math.min(255, inst.amp ?? 0), showParticles: inst.visible !== false }) !== undefined || true; } catch { return false; }
  },
  hasEffect(w, eff) {
    const id = R.fxId(eff);
    if (eff?.mod || eff?.__ro) { const m = MODFX.get(w.e.id)?.[id]; if (m) return m.until > system.currentTick; try { const u = w.e.getDynamicProperty("fx:" + id); return typeof u === "number" && u > system.currentTick; } catch { return false; } }
    try { return !!w.e.getEffect(id); } catch { return false; }
  },
  removeEffect(w, eff) { const id = R.fxId(eff); if (eff?.mod || eff?.__ro) { const m = MODFX.get(w.e.id); if (m) delete m[id]; try { w.e.setDynamicProperty("fx:" + id, undefined); } catch {} return true; } try { w.e.removeEffect(id); } catch {} return true; },
  getEffect(w, eff) { const id = R.fxId(eff); if (eff?.mod || eff?.__ro) { const m = MODFX.get(w.e.id)?.[id]; return m && m.until > system.currentTick ? { __fxi: true, dur: m.until - system.currentTick, amp: m.amp, getDuration() { return this.dur; }, getAmplifier() { return this.amp; } } : null; }
    try { const x = w.e.getEffect(id); return x ? { __fxi: true, dur: x.duration, amp: x.amplifier, getDuration() { return this.dur; }, getAmplifier() { return this.amp; } } : null; } catch { return null; } },
  modEffectsOf(w) { const m = MODFX.get(w.e.id); return m ? Object.entries(m).filter(([k, v]) => v.until > system.currentTick) : []; },
  // ---- commands (Java syntax -> Bedrock; NBT stripped)
  command(src, cmd) {
    let c = String(cmd).replace(/^\//, "");
    // Java-only syntax handled here: particle (dx dy dz speed count), kick (host can't be kicked in single player),
    // fill ... replace water/lava (Java matches flowing states too)
    const pm = /^particle (?:minecraft:)?(\S+)(?: (\S+))? (\S+) (\S+) (\S+)(?: (\S+) (\S+) (\S+) (\S+) (\d+))?/.exec(c);
    if (pm) {
      const lv = src?.level instanceof JLevel ? src.level : (src?.entity instanceof JEntity ? src.entity.level() : JLevel.of(world.getDimension("minecraft:overworld")));
      const base = src?.entity instanceof JEntity ? src.entity.position() : (src?.pos ?? V(0, 64, 0));
      const hasArg = ["block", "block_marker", "falling_dust", "item", "dust", "dust_color_transition"].includes(pm[1]);
      const xyz = hasArg ? [pm[3], pm[4], pm[5]] : [pm[2], pm[3], pm[4]];
      const rel = (t, b) => t?.startsWith("~") ? b + (parseFloat(t.slice(1)) || 0) : parseFloat(t);
      const x = rel(xyz[0], base.x_), y = rel(xyz[1], base.y_), z = rel(xyz[2], base.z_);
      const rest = hasArg ? [pm[6], pm[7], pm[8], pm[9], pm[10]] : [pm[5], pm[6], pm[7], pm[8], pm[9]];
      const type = { particle: hasArg && /stone|granite|cobble|gravel|dirt|deepslate|andesite|diorite/.test(pm[2] ?? "") ? "block_dust" : pm[1] };
      R.particles(lv, type, x, y, z, Math.max(1, parseInt(rest[4]) || 1), parseFloat(rest[0]) || 0, parseFloat(rest[1]) || 0, parseFloat(rest[2]) || 0);
      return 1;
    }
    const km = /^kick (\S+)(?: (.*))?$/.exec(c);
    if (km) c = `title ${km[1]} title §c${km[2] ?? "Disconnected"}`;
    c = R.fixCommand(c);
    if (/^fill .* replace (water|lava)\b/.test(c)) R.command(src, c.replace(/ replace (water|lava)\b/, " replace flowing_$1"));
    try {
      if (src?.entity instanceof JEntity) return src.entity.e.runCommand(c).successCount ?? 0;
      const lv = src?.level instanceof JLevel ? src.level : JLevel.of(world.getDimension("minecraft:overworld"));
      const p = src?.pos ?? V(0, 64, 0);
      return lv.d.runCommand(`execute positioned ${p.x_ ?? p.x} ${p.y_ ?? p.y} ${p.z_ ?? p.z} run ${c}`).successCount ?? 0;
    } catch (e) { warn("command " + c.split(" ")[0] + ": " + e); return 0; }
  },
  fixCommand(c) {
    c = c.replace(/\{[^{}]*(\{[^{}]*\}[^{}]*)*\}/g, "");                 // NBT
    c = c.replace(/^playsound (\S+) (master|music|record|weather|block|hostile|neutral|player|ambient|voice) /, (m, s, cat) => `playsound ${R.soundId({ sound: s.includes(":") ? s : MODID + ":" + s }) ?? s} `);
    c = c.replace(/^stopsound (\S+)(?: \S+)?(?: (\S+))?/, (m, t, s) => `stopsound ${t}${s ? " " + (R.soundId({ sound: s.includes(":") ? s : MODID + ":" + s }) ?? s) : ""}`);
    c = c.replace(/minecraft:/g, "");
    if (/^(fill|setblock|clone|testforblock|execute)\b/.test(c) && CFG.blockJ2B)          // Java block ids -> Bedrock ids
      c = c.replace(/(^|\s)([a-z_]+)(?=\s|$|\[)/g, (m, sp, w) => sp + (CFG.blockJ2B[w] ?? w));
    c = c.replace(new RegExp(MODID + ":", "g"), NS + ":");
    c = c.replace(/^effect give (\S+) (\S+) (\d+) (\d+) true/, "effect $1 $2 $3 $4 true").replace(/^effect give /, "effect ").replace(/^effect clear (\S+) (\S+)/, "effect $1 clear");
    c = c.replace(/^title (\S+) (title|subtitle|actionbar) "?(.*?)"?$/, (m, t, k, txt) => `title ${t} ${k} ${txt.replace(/^\{"text":"(.*)".*\}$/, "$1")}`);
    c = c.replace(/\bweather clear\b/, "weather clear");
    return c;
  },
};
const CLIENT = { getConnection: () => ({ getPlayerInfo: () => ({ getGameMode: () => ({ id: "survival" }) }) }), getSoundManager: () => ({ stop: () => {} }), player: null, level: null };
const CONFIG_BUILDER = { push() { return this; }, pop() { return this; }, comment() { return this; }, translation() { return this; }, worldRestart() { return this; },
  define(name, def) { return { __cfg: true, name, v: def, get() { return this.v; } }; }, defineInRange(name, def) { return { __cfg: true, name, v: def, get() { return this.v; } }; },
  build() { return {}; } };
export { V, bp, JList, JOpt, Stack, BLOCK, DIR, MODFX };
