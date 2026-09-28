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
    if (g_MapReady && StillOwned(slot)) {
        GameRules_SetProp("m_iScriptedHUDFlags", HUD_HIDDEN, 4, slot, true);
        GameRules_SetPropString("m_szScriptedHUDStringSet", "", true, slot);
    }
    g_Hud[slot].owned = false;
}
void ReleaseHud() { for (int s = 0; s < 15; s++) ReleaseSlot(s); }

void PutHud(int region, const char[] text) {
    if (!g_HudReady) return;
    int slot = g_Slots[region];
    if (g_Hud[slot].blocked) return;
    if (g_Hud[slot].owned && !StillOwned(slot)) {
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
    // The shared update flag must remain enabled for other HUD users after unloading.
    // 这个共享的刷新标志在卸载后也要保持开启，其他使用 HUD 的插件依赖它。
    GameRules_SetProp("m_bChallengeModeActive", 1, 1, 0, true);
    GameRules_SetPropFloat("m_fScriptedHUDPosX", g_Layout[region][0], slot, true);
    GameRules_SetPropFloat("m_fScriptedHUDPosY", g_Layout[region][1], slot, true);
    GameRules_SetPropFloat("m_fScriptedHUDWidth", g_Layout[region][2], slot, true);
    GameRules_SetPropFloat("m_fScriptedHUDHeight", g_Layout[region][3], slot, true);
    GameRules_SetPropString("m_szScriptedHUDStringSet", safe, true, slot);
    GameRules_SetProp("m_iScriptedHUDFlags", HUD_FLAGS, 4, slot, true);
    strcopy(g_Hud[slot].text, sizeof(g_Hud[].text), safe);
    for (int i = 0; i < 4; i++) g_Hud[slot].rect[i] = g_Layout[region][i];
    g_Hud[slot].owned = true;
    if (cvDebug.IntValue >= 2) LogMessage("HUD write slot=%d rect=%.2f,%.2f,%.2f,%.2f text=%s",
        slot, g_Layout[region][0], g_Layout[region][1], g_Layout[region][2], g_Layout[region][3], safe);
    if (!StillOwned(slot)) {
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
}
