// Scripted damage credit (e.g. FFVII materia). A caster output records who fired it;
// each apply output then expects a known health drop around its landing time. Only that
// drop, observed by ObserveUnknown, is credited; everything else stays unknown as before.
// 脚本伤害归属（如 FFVII 神器）。施放输出记录是谁触发的；每个生效输出预计在落地时间附近
// 造成一次已知的掉血。只有 ObserveUnknown 观察到的这部分掉血会被归属，其余照旧计入未归属。

void ResetCredits() {
    if (g_Pending != null) g_Pending.Clear();
    if (g_Notices != null) g_Notices.Clear();
    for (int c = 0; c < MAX_CREDITS; c++) { g_CastIdentity[c][0] = '\0'; g_CastName[c][0] = '\0'; g_CastAt[c] = -999.0; }
}

void RegisterCreditHook(Signal signal) {
    if (!signal.output[0]) return;
    char key[160]; int exists;
    Format(key, sizeof(key), "%s:%s", signal.entity.classname, signal.output);
    if (g_CreditHooks.GetValue(key, exists)) return;
    HookEntityOutput(signal.entity.classname, signal.output, CreditOutput);
    g_CreditHooks.SetValue(key, 1);
}

void RegisterCredits() {
    for (int c = 0; c < g_CreditCount; c++) {
        RegisterCreditHook(g_Credit[c].caster);
        for (int a = 0; a < g_Credit[c].applyCount; a++) RegisterCreditHook(g_Credit[c].applies[a]);
    }
}

void UnregisterCredits() {
    StringMapSnapshot keys = g_CreditHooks.Snapshot();
    char key[160], parts[2][80];
    for (int i = 0; i < keys.Length; i++) {
        keys.GetKey(i, key, sizeof(key));
        if (ExplodeString(key, ":", parts, sizeof(parts), sizeof(parts[])) == 2)
            UnhookEntityOutput(parts[0], parts[1], CreditOutput);
    }
    delete keys;
    g_CreditHooks.Clear();
}

public void CreditOutput(const char[] output, int caller, int activator, float delay) {
    if (!g_MapReady || !g_RoundLive || !cvEnable.BoolValue || !cvRank.BoolValue) return;
    for (int c = 0; c < g_CreditCount; c++) {
        if (SignalMatches(caller, output, g_Credit[c].caster)) {
            g_CastAt[c] = GetGameTime();
            g_CastId[c] = ++g_CastSerial;
            g_CastIdentity[c][0] = '\0'; g_CastName[c][0] = '\0';
            int b = g_Credit[c].boss;
            // Same bot rule as weapon damage; an unknown activator leaves the damage unknown.
            // 与武器伤害相同的机器人规则；无法识别的触发者，伤害仍计入未归属。
            if (Survivor(activator) && (!IsFakeClient(activator) || g_Boss[b].bots))
                PlayerIdentity(activator, g_CastIdentity[c], sizeof(g_CastIdentity[]), g_CastName[c], sizeof(g_CastName[]));
            if (cvDebug.IntValue > 0) LogMessage("Credit %s cast by activator=%d identity=%s", g_Credit[c].label, activator, g_CastIdentity[c]);
        }
        for (int a = 0; a < g_Credit[c].applyCount; a++)
            if (SignalMatches(caller, output, g_Credit[c].applies[a])) ApplyCredit(c, a);
    }
}

void ApplyCredit(int c, int a) {
    int b = g_Credit[c].boss;
    float now = GetGameTime();
    if (g_Boss[b].status != 1 || g_Boss[b].ambiguous) return;
    if (!g_CastIdentity[c][0] || now - g_CastAt[c] > CAST_WINDOW) {
        if (cvDebug.IntValue > 0) LogMessage("Credit %s apply %d without a known caster: unknown", g_Credit[c].label, a+1);
        return;
    }
    PendingCredit pending;
    pending.boss = b; pending.serial = g_Boss[b].serial;
    pending.credit = c; pending.cast = g_CastId[c];
    strcopy(pending.identity, sizeof(pending.identity), g_CastIdentity[c]);
    strcopy(pending.name, sizeof(pending.name), g_CastName[c]);
    pending.remaining = g_Credit[c].amounts[a];
    // Allow one tick of scheduling slack before the configured landing time.
    // 在预计落地时间之前留一点调度误差。
    pending.activeFrom = now + g_Credit[c].delays[a] - 0.2;
    pending.activeUntil = now + g_Credit[c].delays[a] + CREDIT_WINDOW;
    if (g_Pending.Length >= MAX_PENDING) g_Pending.Erase(0);
    g_Pending.PushArray(pending);
    if (cvDebug.IntValue > 0) LogMessage("Credit %s apply %d pending %.0f for %s", g_Credit[c].label, a+1, pending.remaining, pending.name);
}

// Credits up to `loss` of an observed health drop to active pending casts, oldest first.
// Returns the credited amount; the caller accounts the rest as unknown.
// 把观察到的掉血按先后顺序分配给正在生效的施放，最多分配 loss；返回已归属的量，剩余由调用者计入未归属。
float AllocateCredit(int b, float loss) {
    if (loss <= 0.0 || g_Pending == null || !g_Pending.Length) return 0.0;
    float now = GetGameTime(), credited = 0.0;
    PendingCredit pending;
    for (int i = 0; i < g_Pending.Length && credited < loss; i++) {
        g_Pending.GetArray(i, pending);
        if (pending.boss != b || pending.serial != g_Boss[b].serial || pending.remaining <= 0.0 ||
            now < pending.activeFrom || now > pending.activeUntil) continue;
        float part = loss - credited;
        if (part > pending.remaining) part = pending.remaining;
        pending.remaining -= part;
        pending.credited += part;
        credited += part;
        g_Pending.SetArray(i, pending);
        AddContribution(b, pending.identity, pending.name, part);
        AddMateria(b, pending, part);
    }
    if (credited > 0.0) g_Boss[b].lastHit = now;
    return credited;
}

void AddMateria(int b, PendingCredit pending, float damage) {
    MateriaRow row;
    ArrayList rows = g_Boss[b].materia;
    for (int i = 0; i < rows.Length; i++) {
        rows.GetArray(i, row);
        if (!StrEqual(row.identity, pending.identity) || !StrEqual(row.label, g_Credit[pending.credit].label)) continue;
        row.damage += damage;
        if (row.lastCast != pending.cast) { row.casts++; row.lastCast = pending.cast; }
        strcopy(row.name, sizeof(row.name), pending.name);
        rows.SetArray(i, row);
        return;
    }
    if (rows.Length >= MAX_CONTRIBUTORS) return;
    strcopy(row.identity, sizeof(row.identity), pending.identity);
    strcopy(row.name, sizeof(row.name), pending.name);
    strcopy(row.label, sizeof(row.label), g_Credit[pending.credit].label);
    row.damage = damage; row.casts = 1; row.lastCast = pending.cast;
    rows.PushArray(row);
}

void SortMateria(ArrayList rows) {
    MateriaRow value, previous;
    for (int i = 1; i < rows.Length; i++) {
        rows.GetArray(i, value);
        int j = i;
        while (j > 0) {
            rows.GetArray(j-1, previous);
            if (previous.damage >= value.damage) break;
            rows.SetArray(j, previous); j--;
        }
        rows.SetArray(j, value);
    }
}

// b < 0: drop expired or finished casts. b >= 0: also announce everything credited to b
// (the boss is settling). Every cast that credited damage is announced exactly once.
// b < 0：移除已过期或已完成的施放。b >= 0：Boss 正在结算，同时播报该 Boss 所有已归属的施放。
// 每次造成伤害的施放只播报一次。
void FlushCredits(int b) {
    if (g_Pending == null) return;
    float now = GetGameTime();
    PendingCredit pending;
    for (int i = 0; i < g_Pending.Length; i++) {
        g_Pending.GetArray(i, pending);
        bool stale = pending.serial != g_Boss[pending.boss].serial;
        bool done = stale || pending.remaining <= 0.0 || now > pending.activeUntil ||
            (pending.boss == b && pending.credited > 0.0);
        if (!done) continue;
        if (!stale && pending.credited > 0.0) AnnounceCredit(pending);
        g_Pending.Erase(i--);
    }
}

void AnnounceCredit(PendingCredit pending) {
    char name[64], boss[64], text[256];
    Utf8Copy(name, sizeof(name), pending.name);
    Utf8Copy(boss, sizeof(boss), g_Config[pending.boss].name);
    if (cvNoticeChat.BoolValue) {
        Format(text, sizeof(text), "[MoreInfo] %s 使用[%s] 对 %s 造成 %.0f 伤害", name, g_Credit[pending.credit].label, boss, pending.credited);
        SurvivorChat(text);
    }
    if (cvNoticeHud.BoolValue) {
        CreditNotice notice;
        Format(notice.text, sizeof(notice.text), "%s 使用[%s] 对 %s 造成 %.0f 伤害", name, g_Credit[pending.credit].label, boss, pending.credited);
        notice.expires = GetGameTime() + cvNoticeHold.FloatValue;
        if (g_Notices.Length >= MAX_NOTICES) g_Notices.Erase(0);
        g_Notices.PushArray(notice);
    }
}

bool NoticeActive() {
    if (g_Notices == null || !cvNoticeHud.BoolValue) return false;
    float now = GetGameTime();
    CreditNotice notice;
    for (int i = 0; i < g_Notices.Length; i++) { g_Notices.GetArray(i, notice); if (notice.expires > now) return true; }
    return false;
}

// Notices borrow the ranking title and rows region, newest first.
// 实时提示借用排行标题和排行行所在的区域，最新的在最上面。
void NoticeText(char lines[7][512]) {
    if (g_Notices == null || !cvNoticeHud.BoolValue) return;
    float now = GetGameTime();
    CreditNotice notice;
    for (int i = g_Notices.Length-1; i >= 0; i--) {
        g_Notices.GetArray(i, notice);
        if (notice.expires <= now) g_Notices.Erase(i);
    }
    if (!g_Notices.Length) return;
    for (int i = 3; i < 7; i++) lines[i][0] = '\0';
    for (int i = 0; i < g_Notices.Length; i++) {
        g_Notices.GetArray(g_Notices.Length-1-i, notice);
        strcopy(lines[3+i], 512, notice.text);
    }
}
