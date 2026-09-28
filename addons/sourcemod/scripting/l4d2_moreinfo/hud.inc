void HudInitialize() {
    char props[][] = {"m_iScriptedHUDFlags", "m_szScriptedHUDStringSet", "m_fScriptedHUDPosX",
        "m_fScriptedHUDPosY", "m_fScriptedHUDWidth", "m_fScriptedHUDHeight", "m_bChallengeModeActive"};
    g_HudReady = true;
    for (int i = 0; i < sizeof(props); i++) {
        if (FindSendPropInfo("CTerrorGameRulesProxy", props[i]) < 0) {
            LogError("HUD unavailable: missing GameRules property %s", props[i]); g_HudReady = false;
        }
    }
    for (int s = 0; s < 15; s++) { g_Hud[s].owned = false; g_Hud[s].blocked = false; }
    if (!g_HudReady) return;
    // Inspect sendprop metadata; SDKTools has no GameRules_GetPropArraySize native.
    // 读取 sendprop 元数据检查数组长度；SDKTools 没有 GameRules_GetPropArraySize。
    int flagsCount, stringsCount;
    FindSendPropInfo("CTerrorGameRulesProxy", "m_iScriptedHUDFlags", _, _, _, flagsCount);
    FindSendPropInfo("CTerrorGameRulesProxy", "m_szScriptedHUDStringSet", _, _, _, stringsCount);
    if (flagsCount < 15 || stringsCount < 15) {
        LogError("HUD unavailable: arrays have fewer than 15 elements"); g_HudReady = false;
    }
}

bool StillOwned(int slot) {
    if (!g_HudReady || !g_Hud[slot].owned) return false;
    char actual[128];
    GameRules_GetPropString("m_szScriptedHUDStringSet", actual, sizeof(actual), slot);
    if (!StrEqual(actual, g_Hud[slot].text) || GameRules_GetProp("m_iScriptedHUDFlags", 4, slot) != HUD_FLAGS) return false;
    return FloatAbs(GameRules_GetPropFloat("m_fScriptedHUDPosX", slot)-g_Hud[slot].rect[0]) < 0.0001 &&
        FloatAbs(GameRules_GetPropFloat("m_fScriptedHUDPosY", slot)-g_Hud[slot].rect[1]) < 0.0001 &&
        FloatAbs(GameRules_GetPropFloat("m_fScriptedHUDWidth", slot)-g_Hud[slot].rect[2]) < 0.0001 &&
        FloatAbs(GameRules_GetPropFloat("m_fScriptedHUDHeight", slot)-g_Hud[slot].rect[3]) < 0.0001;
}

void ReleaseSlot(int slot) {
    if (g_MapReady && g_HudReady && g_Hud[slot].owned) {
        if (g_HudBackend == 1) ScriptSlot(slot, false);
        // Force mode keeps rewriting owned slots, so clear them even if something changed them meanwhile.
        // 强制模式会一直覆盖自己的槽位，所以即使中途被改过也照样清空。
        else if (cvHudFrame.BoolValue || StillOwned(slot)) {
            GameRules_SetProp("m_iScriptedHUDFlags", HUD_HIDDEN, 4, slot, true);
            GameRules_SetPropString("m_szScriptedHUDStringSet", "", true, slot);
        }
    }
    g_Hud[slot].owned = false;
}
void ReleaseHud() {
    for (int s = 0; s < 15; s++) ReleaseSlot(s);
    FlushScriptHud();
}

// ---------- VScript backend (hud_backend 1): inc/smHud.inc, HUDSetLayout via logic_script ----------
// ---------- VScript 后端（hud_backend 1）：用 inc/smHud.inc，经 logic_script 调用 HUDSetLayout ----------

// Escape text for a Squirrel string literal; player names may contain " or \.
// 转义为 Squirrel 字符串字面量；玩家名可能含有 " 或 \。
void ScriptEscape(const char[] text, char[] out, int size) {
    int n;
    for (int i = 0; text[i] && n < size-3; i++) {
        int c = text[i];
        if (c == '\r') continue;
        if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; continue; }
        if (c == '\\' || c == '"') out[n++] = '\\';
        out[n++] = c;
    }
    out[n] = 0;
}

// Queue one slot into ::MyHUD; FlushScriptHud() applies the layout.
// 把一个槽位写进 ::MyHUD；由 FlushScriptHud() 统一应用布局。
void ScriptSlot(int slot, bool visible) {
    if (!g_HudDirty) {
        // ::MyHUD lives in the script VM, which may be recreated; rebuild it when missing.
        // Mark the smHud struct initialised so it never runs script_execute HUDAnimLogic.nut (animations unused).
        // ::MyHUD 在脚本虚拟机里，虚拟机可能被重建，缺失时重新创建。
        // 标记 smHud 已初始化，避免它执行 script_execute HUDAnimLogic.nut（本插件不用动画）。
        L4D2_RunScript("if (!(\"MyHUD\" in getroottable())) ::MyHUD <- { Fields = {} };");
        HUD.isInit = true;
        HUD.tableCreated = true;
        g_HudDirty = true;
    }
    char text[260];
    if (visible) ScriptEscape(g_Hud[slot].text, text, sizeof(text));
    HUD.AddNewHUD_asTEXT(slot, text, visible ? HUD_FLAGS : HUD_HIDDEN);
    if (visible) HUD.Place(slot, g_Hud[slot].rect[2], g_Hud[slot].rect[3], g_Hud[slot].rect[0], g_Hud[slot].rect[1]);
    g_HudSent[slot] = GetGameTime();
}

void FlushScriptHud() {
    if (!g_HudDirty) return;
    g_HudDirty = false;
    GameRules_SetProp("m_bChallengeModeActive", 1, 1, 0, true);
    HUD.Draw();
}

// Resend owned slots whose networked text no longer matches (another HUDSetLayout replaced ours).
// 联网文字和本插件不一致时（别处的 HUDSetLayout 替换了我们的布局）重新发送。
void ScriptWatch() {
    float now = GetGameTime();
    bool any;
    for (int s = 0; s < 15; s++) {
        if (!g_Hud[s].owned || g_Hud[s].blocked) continue;
        any = true;
        // HUDSetLayout may apply on the next think; give it time before comparing.
        // HUDSetLayout 可能在下一次 think 才生效，比较前先等一会。
        if (now-g_HudSent[s] < 0.5) continue;
        char actual[128];
        GameRules_GetPropString("m_szScriptedHUDStringSet", actual, sizeof(actual), s);
        if (!StrEqual(actual, g_Hud[s].text)) { HudStomped(s); ScriptSlot(s, true); }
    }
    if (any && !GameRules_GetProp("m_bChallengeModeActive", 1)) {
        HudStomped(-1);
        GameRules_SetProp("m_bChallengeModeActive", 1, 1, 0, true);
    }
    FlushScriptHud();
}

// Write the stored text/rect of a slot to GameRules. / 把槽位保存的文字和位置写入 GameRules。
void WriteSlot(int slot) {
    // The shared update flag must remain enabled for other HUD users after unloading.
    // 这个共享的刷新标志在卸载后也要保持开启，其他使用 HUD 的插件依赖它。
    GameRules_SetProp("m_bChallengeModeActive", 1, 1, 0, true);
    GameRules_SetPropFloat("m_fScriptedHUDPosX", g_Hud[slot].rect[0], slot, true);
    GameRules_SetPropFloat("m_fScriptedHUDPosY", g_Hud[slot].rect[1], slot, true);
    GameRules_SetPropFloat("m_fScriptedHUDWidth", g_Hud[slot].rect[2], slot, true);
    GameRules_SetPropFloat("m_fScriptedHUDHeight", g_Hud[slot].rect[3], slot, true);
    GameRules_SetPropString("m_szScriptedHUDStringSet", g_Hud[slot].text, true, slot);
    GameRules_SetProp("m_iScriptedHUDFlags", HUD_FLAGS, 4, slot, true);
}

// Force mode: rewrite owned slots in case something resets the scripted HUD.
// hud_frame_force 1 runs this from the repeating Tick timer (~0.1 s), 2 every game frame.
// Counts how often the values were found changed, so the test shows whether anything interferes.
// 强制模式：重写自己的槽位，防止脚本 HUD 被别的东西重置。
// hud_frame_force 为 1 时由循环计时器 Tick 执行（约 0.1 秒一次），为 2 时每帧执行。
// 同时统计发现被改动的次数，测试时可以看出是否真有东西在干扰。
void HudFrame(int mode) {
    if (!g_HudReady || !g_MapReady || cvHudFrame.IntValue != mode) return;
    if (g_HudBackend == 1) { ScriptWatch(); return; }
    bool any;
    for (int s = 0; s < 15; s++) {
        if (!g_Hud[s].owned || g_Hud[s].blocked) continue;
        if (!StillOwned(s)) HudStomped(s);
        WriteSlot(s);
        any = true;
    }
    if (any && !GameRules_GetProp("m_bChallengeModeActive", 1)) HudStomped(-1);
}

void HudStomped(int slot) {
    g_HudStomps++;
    // At most one log line per second; the reset may repeat every frame.
    // 每秒最多记一条日志，重置可能每帧都发生。
    float now = GetGameTime();
    if (cvDebug.IntValue >= 1 && now >= g_HudStompLog) {
        g_HudStompLog = now + 1.0;
        if (slot < 0) LogMessage("HUD m_bChallengeModeActive was reset to 0; rewritten (total %d)", g_HudStomps);
        else LogMessage("HUD slot %d was changed externally; rewritten (total %d)", slot, g_HudStomps);
    }
}

void PutHud(int region, const char[] text) {
    if (!g_HudReady) return;
    int slot = g_Slots[region];
    if (g_Hud[slot].blocked) return;
    bool script = g_HudBackend == 1;
    if (!script && g_Hud[slot].owned && !cvHudFrame.BoolValue && !StillOwned(slot)) {
        g_Hud[slot].owned = false; g_Hud[slot].blocked = true;
        LogError("HUD slot %d changed externally; stopped writing until reload", slot);
        return;
    }
    if (!text[0]) { ReleaseSlot(slot); return; }
    if (!g_Hud[slot].owned) {
        char actual[128]; GameRules_GetPropString("m_szScriptedHUDStringSet", actual, sizeof(actual), slot);
        int flags = GameRules_GetProp("m_iScriptedHUDFlags", 4, slot);
        if (actual[0] && !(flags & HUD_HIDDEN)) {
            g_Hud[slot].blocked = true;
            LogError("HUD slot %d is occupied; change slot cvars or layout", slot);
            return;
        }
    }
    char safe[128]; Utf8Copy(safe, sizeof(safe), text);
    if (g_Hud[slot].owned && StrEqual(safe, g_Hud[slot].text)) return;
    strcopy(g_Hud[slot].text, sizeof(g_Hud[].text), safe);
    for (int i = 0; i < 4; i++) g_Hud[slot].rect[i] = g_Layout[region][i];
    if (script) ScriptSlot(slot, true);
    else WriteSlot(slot);
    g_Hud[slot].owned = true;
    if (cvDebug.IntValue >= 2) LogMessage("HUD write (%s) slot=%d rect=%.2f,%.2f,%.2f,%.2f text=%s", script ? "vscript" : "netprop",
        slot, g_Layout[region][0], g_Layout[region][1], g_Layout[region][2], g_Layout[region][3], safe);
    // VScript applies the layout asynchronously, so only the netprop backend can read back immediately.
    // VScript 布局是异步生效的，只有 netprop 后端能立即回读校验。
    if (!script && !StillOwned(slot)) {
        g_Hud[slot].owned = false; g_Hud[slot].blocked = true;
        LogError("HUD slot %d failed readback; backend disabled for this slot", slot);
    }
}

void RenderHud() {
    char lines[7][512];
    float now = GetGameTime();
    if (cvHp.BoolValue) {
        int active[MAX_BOSSES], count;
        for (int b = 0; b < g_BossCount; b++)
            if (g_Boss[b].status == 1 && !g_Boss[b].ambiguous && now-g_Boss[b].lastHit <= cvHpHold.FloatValue) active[count++] = b;
        if (count) {
            int b = active[RoundToFloor(now / 1.5) % count];
            char shortName[64]; Utf8Copy(shortName, sizeof(shortName), g_Config[b].name);
            if (g_Boss[b].maxHp > 0.0)
                Format(lines[0], 512, "%s 阶段%d: %.0f / %.0f", shortName, g_Boss[b].phase+1, g_Boss[b].hp, g_Boss[b].maxHp);
            else Format(lines[0], 512, "%s 阶段%d: %.0f / 未知", shortName, g_Boss[b].phase+1, g_Boss[b].hp);
        }
    }
    if (cvMsg.BoolValue) MessageText(lines);
    RankingText(lines);
    NoticeText(lines);
    if (now < g_HudTestUntil) {
        // Self-test: fill every region so rendering can be checked without boss/message state.
        // 自检：填满所有区域，不需要 Boss 或地图消息也能检查显示效果。
        char names[7][] = {"boss", "message_1", "message_2", "rank_title", "rank_1", "rank_2", "rank_3"};
        for (int i = 0; i < 7; i++)
            Format(lines[i], 512, "[TEST] region=%d slot=%d %s 测试 %.0f", i, g_Slots[i], names[i], g_HudTestUntil-now);
    }
    for (int i = 0; i < 7; i++) PutHud(i, lines[i]);
    FlushScriptHud();
}
