char g_RegionNames[7][] = {"Boss血量", "消息1", "消息2", "排行标题", "排行1", "排行2", "排行3"};

// Readable names for BossState.status (0..4, 5 = out of range). / BossState.status（0..4，5 为越界）的文字说明。
char g_StatusNames[6][] = {"等待开始", "进行中", "结束中", "已结算", "已取消", "未知"};

#define OnOff(%1) ((%1) ? "开" : "关")
#define YesNo(%1) ((%1) ? "是" : "否")
#define StatusName(%1) g_StatusNames[(%1) >= 0 && (%1) < 5 ? (%1) : 5]

public Action CommandStatus(int client, int argc) {
    ReplyToCommand(client, "[MoreInfo] 版本 %s | 总开关:%s 血量:%s 消息:%s 排行:%s 排行HUD:%s 排行聊天:%s | HUD可用:%s 待显示排行:%d",
        VERSION, OnOff(cvEnable.BoolValue), OnOff(cvHp.BoolValue), OnOff(cvMsg.BoolValue), OnOff(cvRank.BoolValue),
        OnOff(cvRankHud.BoolValue), OnOff(cvRankChat.BoolValue), YesNo(g_HudReady), g_Results.Length);
    ReplyToCommand(client, "[MoreInfo] Boss:%d 消息规则:%d 翻译:%d 神器归属:%d 待生效:%d%s", g_BossCount, g_RuleCount, g_TrCount,
        g_CreditCount, g_Pending.Length, g_BossCount ? "" : "（当前地图未配置 Boss）");
    ReplyToCommand(client, "[MoreInfo] 当前消息:%d 过滤模式:%s | 消息钩子 SayText:%s SayText2:%s TextMsg:%s",
        g_Messages.Length, cvFilter.IntValue ? "宽松" : "严格",
        YesNo(g_SayText != INVALID_MESSAGE_ID), YesNo(g_SayText2 != INVALID_MESSAGE_ID), YesNo(g_TextMsg != INVALID_MESSAGE_ID));
    if (cvMsg.BoolValue && cvFilter.IntValue == 0 && g_RuleCount == 0)
        ReplyToCommand(client, "[MoreInfo] 严格模式没有消息规则，请配置 maps/<当前地图名>/config.cfg 的 messages。");
    char forceNames[3][] = {"关", "计时器", "每帧"};
    int force = cvHudFrame.IntValue;
    ReplyToCommand(client, "[MoreInfo] 强制刷新HUD:%s 发现被外部改动:%d 次 挑战模式标志:%d",
        forceNames[force >= 0 && force <= 2 ? force : 0], g_HudStomps, g_HudReady ? GameRules_GetProp("m_bChallengeModeActive", 1) : -1);
    for (int i = 0; i < 7; i++) {
        int slot = g_Slots[i];
        ReplyToCommand(client, "[MoreInfo] HUD区域 %s: 槽位%d %s", g_RegionNames[i], slot,
            g_Hud[slot].blocked ? "被占用/已停止写入" : (g_Hud[slot].owned ? "显示中" : "空闲"));
    }
    for (int b = 0; b < g_BossCount; b++) {
        char hp[48];
        if (g_Boss[b].maxHp > 0.0) Format(hp, sizeof(hp), "%.1f / %.1f", g_Boss[b].hp, g_Boss[b].maxHp);
        else Format(hp, sizeof(hp), "%.1f / 未知", g_Boss[b].hp);
        ReplyToCommand(client, "[MoreInfo] %s（第%d场）: %s 阶段%d/%d 血量 %s 实体引用:%d 未归属:%.1f 贡献人数:%d 神器记录:%d%s%s",
            g_Config[b].id, g_Boss[b].serial, StatusName(g_Boss[b].status), g_Boss[b].phase+1, g_Config[b].phaseCount,
            hp, g_Boss[b].healthRef, g_Boss[b].unknown, g_Boss[b].contributions.Length, g_Boss[b].materia.Length,
            g_Boss[b].partial ? " 统计不完整" : "", g_Boss[b].ambiguous ? " 选择器冲突(已停用)" : "");
    }
    return Plugin_Handled;
}

public Action CommandHudTest(int client, int argc) {
    float seconds = 15.0;
    if (argc) { char arg[16]; GetCmdArg(1, arg, sizeof(arg)); seconds = StringToFloat(arg); }
    if (seconds < 0.0) seconds = 0.0;
    if (seconds > 120.0) seconds = 120.0;
    g_HudTestUntil = seconds > 0.0 ? GetGameTime() + seconds : 0.0;
    // Clear stale blocks so the test reflects the current slot state.
    // 清除旧的屏蔽标记，让自检反映槽位当前的真实状态。
    for (int s = 0; s < 15; s++) g_Hud[s].blocked = false;
    g_NextHud = 0.0;
    if (seconds <= 0.0) { ReplyToCommand(client, "[MoreInfo] HUD 自检已关闭。"); return Plugin_Handled; }
    ReplyToCommand(client, "[MoreInfo] HUD 自检 %.0f 秒 | HUD可用:%s 地图已就绪:%s 回合进行中:%s 总开关:%s",
        seconds, YesNo(g_HudReady), YesNo(g_MapReady), YesNo(g_RoundLive), OnOff(cvEnable.BoolValue));
    return Plugin_Handled;
}

public Action CommandReload(int client, int argc) {
    char error[256];
    if (LoadConfiguration(true, error, sizeof(error))) ReplyToCommand(client, "[MoreInfo] 配置已重载。");
    else ReplyToCommand(client, "[MoreInfo] 拒绝重载，保留原配置: %s", error);
    return Plugin_Handled;
}

void EntityDescription(int e, char[] text, int size) {
    char classname[64], name[128], boss[80];
    GetEntityClassname(e, classname, sizeof(classname));
    if (HasEntProp(e, Prop_Data, "m_iName")) GetEntPropString(e, Prop_Data, "m_iName", name, sizeof(name));
    int hammer = HasEntProp(e, Prop_Data, "m_iHammerID") ? GetEntProp(e, Prop_Data, "m_iHammerID") : -1;
    int health = HasEntProp(e, Prop_Data, "m_iHealth") ? GetEntProp(e, Prop_Data, "m_iHealth") : -1;
    int owner = e >= 0 && e < MAX_ENTS ? g_EntityBoss[e] : -1;
    if (owner >= 0 && owner < g_BossCount) strcopy(boss, sizeof(boss), g_Config[owner].id);
    else strcopy(boss, sizeof(boss), owner == -2 ? "冲突" : "无");
    Format(text, size, "实体:%d 引用:%d 类名:%s 名称:%s hammer_id:%d 血量:%d 计数器偏移:%d 所属Boss:%s",
        e, EntIndexToEntRef(e), classname, name[0] ? name : "(无)", hammer, health, FindDataMapInfo(e, "m_OutValue"), boss);
}

public Action CommandProbe(int client, int argc) {
    int e = -1;
    if (argc) { char arg[16]; GetCmdArg(1, arg, sizeof(arg)); e = StringToInt(arg); }
    else if (client > 0) e = GetClientAimTarget(client, false);
    if (e <= MaxClients || !IsValidEntity(e)) { ReplyToCommand(client, "[MoreInfo] 请瞄准一个实体，或使用 sm_moreinfo_probe <实体编号>"); return Plugin_Handled; }
    char text[512]; EntityDescription(e, text, sizeof(text)); ReplyToCommand(client, "[MoreInfo] %s", text);
    return Plugin_Handled;
}

public Action CommandDump(int client, int argc) {
    char path[PLATFORM_MAX_PATH], text[512];
    BuildPath(Path_SM, path, sizeof(path), "logs/l4d2_moreinfo_entities.log");
    File file = OpenFile(path, "w");
    if (file == null) { ReplyToCommand(client, "[MoreInfo] 无法写入 %s", path); return Plugin_Handled; }
    char map[128]; GetCurrentMap(map, sizeof(map)); file.WriteLine("MoreInfo %s 地图:%s", VERSION, map);
    for (int e = MaxClients+1; e < GetMaxEntities(); e++) {
        if (!IsValidEntity(e)) continue;
        EntityDescription(e, text, sizeof(text)); file.WriteLine("%s", text);
    }
    delete file;
    ReplyToCommand(client, "[MoreInfo] 实体诊断已写入 %s", path);
    CommandStatus(client, 0);
    return Plugin_Handled;
}
