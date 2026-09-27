void ResetBoss(int b, bool partial) {
    g_Boss[b].serial = ++g_Serial;
    g_Boss[b].phase = 0;
    g_Boss[b].healthRef = INVALID_ENT_REFERENCE;
    g_Boss[b].status = 0;
    g_Boss[b].hp = 0.0; g_Boss[b].maxHp = 0.0;
    g_Boss[b].lastHit = -999.0;
    g_Boss[b].unknown = 0.0;
    g_Boss[b].partial = partial;
    g_Boss[b].bots = cvBots.BoolValue;
    g_Boss[b].ambiguous = false;
    g_Boss[b].startSeen = false;
    g_Boss[b].startPending = false;
    g_Boss[b].startDamaged = false;
    g_Boss[b].startAt = 0.0;
    g_Boss[b].contributions.Clear();
}

void RegisterSignal(Signal signal) {
    if (!signal.output[0]) return;
    char key[160]; int exists;
    Format(key, sizeof(key), "%s:%s", signal.entity.classname, signal.output);
    if (g_OutputHooks.GetValue(key, exists)) return;
    HookEntityOutput(signal.entity.classname, signal.output, BossOutput);
    g_OutputHooks.SetValue(key, 1);
}

void RegisterSignals() {
    for (int b = 0; b < g_BossCount; b++) {
        RegisterSignal(g_Config[b].cancel);
        for (int p = 0; p < g_Config[b].phaseCount; p++) {
            RegisterSignal(g_Config[b].phases[p].start);
            RegisterSignal(g_Config[b].phases[p].finish);
        }
    }
}

void UnregisterSignals() {
    StringMapSnapshot keys = g_OutputHooks.Snapshot();
    char key[160], parts[2][80];
    for (int i = 0; i < keys.Length; i++) {
        keys.GetKey(i, key, sizeof(key));
        if (ExplodeString(key, ":", parts, sizeof(parts), sizeof(parts[])) == 2)
            UnhookEntityOutput(parts[0], parts[1], BossOutput);
    }
    delete keys;
    g_OutputHooks.Clear();
}

bool SignalMatches(int caller, const char[] output, Signal signal) {
    return signal.output[0] && StrEqual(signal.output, output) && Matches(caller, signal.entity);
}

// A signal fired by the bound health entity. func_breakable clears its targetname
// before firing OnBreak, so selector matching fails at the exact moment of death.
// 由已绑定的血量实体发出的信号。func_breakable 触发 OnBreak 前会清空 targetname，
// 所以死亡那一刻用选择器匹配会失败。
bool BoundSignal(int b, int caller, const char[] output, Signal signal) {
    int p = g_Boss[b].phase;
    if (!signal.output[0] || !StrEqual(signal.output, output) || g_Boss[b].healthRef == INVALID_ENT_REFERENCE ||
        !SameSelector(signal.entity, g_Config[b].phases[p].health)) return false;
    int ref = caller >= 0 ? EntIndexToEntRef(caller) : caller;
    return ref == g_Boss[b].healthRef;
}

bool SignalFrom(int b, int caller, const char[] output, Signal signal, bool &bound) {
    bound = BoundSignal(b, caller, output, signal);
    return bound || SignalMatches(caller, output, signal);
}

void ActivateBoss(int b, bool confirmedStart) {
    int p = g_Boss[b].phase;
    float hp;
    if (g_Boss[b].ambiguous || !CurrentHealth(b, hp)) return;
    g_Boss[b].hp = hp;
    g_Boss[b].maxHp = g_Config[b].phases[p].fixedMax;
    if (g_Boss[b].maxHp <= 0.0 && confirmedStart && g_Config[b].phases[p].captureAtStart)
        g_Boss[b].maxHp = hp;
    // m_iMaxHealth is used only if it actually exists and is positive.
    // 只有 m_iMaxHealth 存在且为正数时才使用它。
    int e = g_Boss[b].healthRef;
    if (g_Boss[b].maxHp <= 0.0 && g_Config[b].phases[p].model == 0 && HasEntProp(e, Prop_Data, "m_iMaxHealth"))
        g_Boss[b].maxHp = Positive(float(GetEntProp(e, Prop_Data, "m_iMaxHealth")));
    // Scripts often SetHealth without touching m_iMaxHealth; never show hp above max.
    // 地图脚本常用 SetHealth 却不改 m_iMaxHealth；显示时血量不超过上限。
    if (g_Boss[b].maxHp > 0.0 && hp > g_Boss[b].maxHp) g_Boss[b].maxHp = hp;
    if (!confirmedStart) g_Boss[b].partial = true;
    if (p == 0) g_Boss[b].bots = cvBots.BoolValue;
    g_Boss[b].status = 1;
    if (cvDebug.IntValue > 0) LogMessage("Boss %s instance=%d phase=%d hp=%.1f max=%.1f partial=%d",
        g_Config[b].id, g_Boss[b].serial, p+1, hp, g_Boss[b].maxHp, g_Boss[b].partial);
}

void ScanBindings() {
    if (!g_MapReady || !g_RoundLive || !cvEnable.BoolValue) return;
    for (int e = 0; e < MAX_ENTS; e++) g_EntityBoss[e] = -1;
    for (int b = 0; b < g_BossCount; b++) {
        if (g_Boss[b].status >= 2) continue;
        int p = g_Boss[b].phase;
        int health = FindUnique(g_Config[b].phases[p].health);
        bool bad = health == -2;
        int hits[MAX_HITS];
        for (int h = 0; h < g_Config[b].phases[p].hitCount; h++) {
            hits[h] = FindUnique(g_Config[b].phases[p].hits[h]);
            if (hits[h] == -2) bad = true;
        }
        if (bad) {
            if (!g_Boss[b].ambiguous) LogError("Ambiguous selectors for boss %s; binding disabled", g_Config[b].id);
            g_Boss[b].ambiguous = true;
            g_Boss[b].partial = true;
            continue;
        }
        g_Boss[b].ambiguous = false;
        if (health == -1 || NotEntity(health)) continue;
        int reference = health >= 0 ? EntIndexToEntRef(health) : health;
        if (g_Boss[b].status == 1 && g_Boss[b].healthRef != reference) {
            g_Boss[b].status = 4;
            LogError("Boss %s health entity replaced: fight cancelled", g_Config[b].id);
            continue;
        }
        g_Boss[b].healthRef = reference;
        for (int h = 0; h < g_Config[b].phases[p].hitCount; h++) {
            int e = hits[h];
            if (e <= MaxClients || e >= MAX_ENTS) continue;
            if (g_EntityBoss[e] != -1 && g_EntityBoss[e] != b) {
                int other = g_EntityBoss[e];
                if (other >= 0) { g_Boss[other].ambiguous = true; g_Boss[other].partial = true; }
                g_Boss[b].ambiguous = true; g_Boss[b].partial = true;
                g_EntityBoss[e] = -2;
                LogError("Shared hitbox %d: boss binding disabled", e);
                continue;
            }
            g_EntityBoss[e] = b;
            g_EntityRef[e] = EntIndexToEntRef(e);
            if (!g_Hooked[e]) {
                SDKHook(e, SDKHook_OnTakeDamage, DamagePre);
                SDKHook(e, SDKHook_OnTakeDamagePost, DamagePost);
                g_Hooked[e] = true;
            }
        }
        if (g_Boss[b].status == 0 && !g_Boss[b].startPending &&
            (!g_Config[b].phases[p].start.output[0] || g_Late || g_Boss[b].partial)) ActivateBoss(b, false);
    }
}

public void ScanFrame(any epoch) { g_ScanQueued = false; if (epoch == g_Epoch) ScanBindings(); }

public void OnEntityCreated(int entity, const char[] classname) {
    if (entity <= MaxClients || entity >= MAX_ENTS) return;
    g_Hooked[entity] = false; g_EntityBoss[entity] = -1;
    if (g_MapReady && cvEnable != null && cvEnable.BoolValue) SDKHook(entity, SDKHook_SpawnPost, Spawned);
}
public void Spawned(int entity) {
    if (!g_ScanQueued) { g_ScanQueued = true; RequestFrame(ScanFrame, g_Epoch); }
}

public void OnEntityDestroyed(int entity) {
    if (entity <= MaxClients || entity >= MAX_ENTS) return;
    g_Hooked[entity] = false; g_EntityBoss[entity] = -1;
    // Never infer death from removal. A confirmed finish has already set status 2.
    // 不根据实体删除推断死亡；确认的结束信号已经把状态设为 2。
    if (!g_MapReady) return;
    for (int b = 0; b < g_BossCount; b++) {
        if (g_Boss[b].status == 1 && EntRefToEntIndex(g_Boss[b].healthRef) == entity) {
            g_Boss[b].status = 4;
            LogMessage("Boss %s removed without finish output: cancelled", g_Config[b].id);
        }
    }
}

public void BossOutput(const char[] output, int caller, int activator, float delay) {
    if (!g_MapReady || !g_RoundLive || !cvEnable.BoolValue) return;
    for (int b = 0; b < g_BossCount; b++) {
        int p = g_Boss[b].phase;
        if (g_Boss[b].ambiguous) continue;
        if (g_Boss[b].status >= 3 && SignalMatches(caller, output, g_Config[b].phases[0].start)) {
            ResetBoss(b, false);
            p = 0;
        }
        bool bound;
        if (SignalFrom(b, caller, output, g_Config[b].cancel, bound)) {
            g_Boss[b].status = 4;
            continue;
        }
        if (g_Boss[b].status <= 1 && !g_Boss[b].startSeen && SignalMatches(caller, output, g_Config[b].phases[p].start)) {
            int unique = FindUnique(g_Config[b].phases[p].start.entity);
            if (unique != caller && (unique < 0 || EntIndexToEntRef(unique) != caller)) {
                g_Boss[b].partial = true;
                LogError("Ambiguous start signal for %s", g_Config[b].id);
                continue;
            }
            // A real start supersedes provisional binding from a late plugin load.
            // 真正的开始信号会取代插件中途加载时的临时绑定。
            if (p == 0) ResetBoss(b, false);
            g_Boss[b].startSeen = true;
            g_Boss[b].startPending = true;
            g_Boss[b].startDamaged = false;
            g_Boss[b].startAt = GetGameTime() + g_Config[b].phases[p].startDelay;
            ScanBindings();
            if (g_Config[b].phases[p].startDelay == 0.0) {
                ActivateBoss(b, true);
                g_Boss[b].startPending = false;
            }
        }
        if (g_Boss[b].status == 0 && g_Boss[b].startPending && SignalFrom(b, caller, output, g_Config[b].phases[p].finish, bound)) {
            ActivateBoss(b, false);
            g_Boss[b].startPending = false;
        }
        if (g_Boss[b].status != 1 || !SignalFrom(b, caller, output, g_Config[b].phases[p].finish, bound)) continue;
        if (!bound && FindUnique(g_Config[b].phases[p].finish.entity) != caller) {
            g_Boss[b].partial = true;
            LogError("Ambiguous finish signal for %s", g_Config[b].id);
            continue;
        }
        g_Boss[b].status = 2;
        DataPack pack = new DataPack();
        pack.WriteCell(g_Epoch); pack.WriteCell(b); pack.WriteCell(g_Boss[b].serial); pack.WriteCell(p);
        RequestFrame(FinishFrame, pack);
    }
    if (cvDebug.IntValue >= 2) LogMessage("output=%s caller=%d activator=%d delay=%.3f depth=%d", output, caller, activator, delay, g_Depth);
}

public void FinishFrame(any data) {
    DataPack pack = view_as<DataPack>(data); pack.Reset();
    int epoch = pack.ReadCell(), b = pack.ReadCell(), serial = pack.ReadCell(), phase = pack.ReadCell();
    delete pack;
    if (epoch != g_Epoch || !cvEnable.BoolValue || !g_RoundLive || b >= g_BossCount ||
        g_Boss[b].serial != serial || g_Boss[b].phase != phase || g_Boss[b].status != 2) return;
    ObserveUnknown(b);
    if (phase + 1 < g_Config[b].phaseCount) {
        g_Boss[b].phase++;
        g_Boss[b].status = 0;
        g_Boss[b].healthRef = INVALID_ENT_REFERENCE;
        g_Boss[b].lastHit = -999.0;
        g_Boss[b].startSeen = false;
        g_Boss[b].startPending = false;
        g_Boss[b].startDamaged = false;
        ScanBindings();
    } else {
        g_Boss[b].status = 3;
        if (cvRank.BoolValue) QueueResult(b);
    }
}

void ObserveUnknown(int b) {
    float hp;
    if (!CurrentHealth(b, hp)) {
        if (g_Boss[b].hp > 0.0) g_Boss[b].partial = true;
        return;
    }
    float loss = Positive(g_Boss[b].hp - hp);
    if (cvRank.BoolValue) g_Boss[b].unknown += loss;
    g_Boss[b].hp = hp;
}

void PollBosses() {
    for (int b = 0; b < g_BossCount; b++) {
        if (g_Boss[b].status == 0 && g_Boss[b].startPending && GetGameTime() >= g_Boss[b].startAt) {
            ScanBindings();
            ActivateBoss(b, !g_Boss[b].startDamaged);
            g_Boss[b].startPending = false;
        }
        if (g_Boss[b].status == 1 && !g_Boss[b].ambiguous) {
            if (EntRefToEntIndex(g_Boss[b].healthRef) == INVALID_ENT_REFERENCE) { g_Boss[b].status = 4; continue; }
            ObserveUnknown(b);
        }
    }
}

public Action DamagePre(int victim, int &attacker, int &inflictor, float &damage, int &damagetype) {
    if (victim <= MaxClients || victim >= MAX_ENTS || !cvEnable.BoolValue || !g_RoundLive) return Plugin_Continue;
    int b = g_EntityBoss[victim];
    if (b < 0 || g_Boss[b].ambiguous || g_EntityRef[victim] != EntIndexToEntRef(victim)) return Plugin_Continue;
    // Damage before the configured sampling time invalidates a full-HP sample.
    // Do not account the map's temporary spawn health as real boss damage.
    // 在设定的采样时间之前受到伤害，满血采样作废；地图出生时的临时血量不计为 Boss 伤害。
    if (g_Boss[b].status == 0 && g_Boss[b].startPending) {
        g_Boss[b].startDamaged = true;
        g_Boss[b].partial = true;
        return Plugin_Continue;
    }
    // Start signal was missed (e.g. unexpected map I/O): show HP anyway, statistics partial.
    // 错过了开始信号（例如地图 I/O 异常）：照常显示血量，统计标记为不完整。
    if (g_Boss[b].status == 0 && !g_Boss[b].startPending && Survivor(attacker) &&
        EntIndexToEntRef(victim) == g_Boss[b].healthRef) {
        if (cvDebug.IntValue > 0) LogMessage("Boss %s damaged before start signal: provisional activation", g_Config[b].id);
        ActivateBoss(b, false);
    }
    if (g_Boss[b].status != 1) return Plugin_Continue;
    if (g_Depth >= MAX_CONTEXTS) {
        g_Overflow++;
        g_Boss[b].partial = true;
        for (int i = 0; i < g_Depth; i++) g_Context[i].uncertain = true;
        return Plugin_Continue;
    }
    DamageContext ctx;
    ctx.victim = victim;
    ctx.entityRef = EntIndexToEntRef(victim); ctx.boss = b;
    ctx.serial = g_Boss[b].serial; ctx.phase = g_Boss[b].phase;
    ctx.healthRef = g_Boss[b].healthRef;
    if (!CurrentHealth(b, ctx.before)) { g_Boss[b].partial = true; return Plugin_Continue; }
    if (g_Depth == 0) ObserveUnknown(b);
    if (Survivor(attacker)) {
        g_Boss[b].lastHit = GetGameTime();
        if (!IsFakeClient(attacker) || g_Boss[b].bots)
            PlayerIdentity(attacker, ctx.identity, sizeof(ctx.identity), ctx.name, sizeof(ctx.name));
    }
    g_Context[g_Depth++] = ctx;
    if (cvDebug.IntValue >= 2) LogMessage("pre boss=%s victim=%d attacker=%d inflictor=%d raw=%.2f hp=%.2f depth=%d",
        g_Config[b].id, victim, attacker, inflictor, damage, ctx.before, g_Depth);
    return Plugin_Continue;
}

public void DamagePost(int victim, int attacker, int inflictor, float damage, int damagetype) {
    if (g_Overflow > 0) { g_Overflow--; return; }
    if (!g_Depth || victim <= MaxClients || victim >= MAX_ENTS) return;
    // Compare index encoded in the serial-safe reference even if removal occurred during damage.
    // 即使实体在伤害过程中被删除，也用带序列号的引用来比较。
    int current = g_Depth - 1;
    if (g_Context[current].victim != victim) return;
    DamageContext ctx; ctx = g_Context[--g_Depth];
    int b = ctx.boss;
    if (!cvEnable.BoolValue || !g_RoundLive || b >= g_BossCount || ctx.serial != g_Boss[b].serial ||
        ctx.phase != g_Boss[b].phase || ctx.healthRef != g_Boss[b].healthRef || g_Boss[b].status >= 3) return;
    float after;
    bool readable = CurrentHealth(b, after);
    if (!readable) {
        // Only an explicitly confirmed OnBreak on the health body proves final zero HP.
        // 只有血量实体上明确确认的 OnBreak 才能证明血量归零。
        if (g_Boss[b].status == 2 && g_Config[b].phases[ctx.phase].model == 0 &&
            ctx.entityRef == ctx.healthRef && StrEqual(g_Config[b].phases[ctx.phase].finish.output, "OnBreak") &&
            SameSelector(g_Config[b].phases[ctx.phase].finish.entity, g_Config[b].phases[ctx.phase].health)) after = 0.0;
        else { g_Boss[b].partial = true; return; }
    }
    float total = Positive(ctx.before - after);
    float effective = Positive(total - ctx.nested);
    for (int i = g_Depth - 1; i >= 0; i--) {
        if (g_Context[i].boss == b && g_Context[i].healthRef == ctx.healthRef) {
            g_Context[i].nested += total;
            if (after > ctx.before) g_Context[i].uncertain = true;
            break;
        }
    }
    bool verified = g_Config[b].phases[ctx.phase].synchronous;
    if (cvRank.BoolValue && effective > 0.0) {
        if (verified && !ctx.uncertain && ctx.identity[0]) AddContribution(b, ctx.identity, ctx.name, effective);
        else g_Boss[b].unknown += effective;
    }
    g_Boss[b].hp = after;
    if (ctx.uncertain) g_Boss[b].partial = true;
    if (cvDebug.IntValue >= 2) LogMessage("post boss=%s hp=%.2f effective=%.2f verified=%d depth=%d",
        g_Config[b].id, after, effective, verified, g_Depth);
}
