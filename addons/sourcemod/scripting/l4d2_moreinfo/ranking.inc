void AddContribution(int b, const char[] identity, const char[] name, float damage) {
    Contribution row;
    ArrayList rows = g_Boss[b].contributions;
    for (int i = 0; i < rows.Length; i++) {
        rows.GetArray(i, row);
        if (!StrEqual(row.identity, identity)) continue;
        row.damage += damage;
        strcopy(row.name, sizeof(row.name), name);
        rows.SetArray(i, row);
        return;
    }
    if (rows.Length >= MAX_CONTRIBUTORS) {
        g_Boss[b].unknown += damage;
        if (!g_Boss[b].partial) LogError("Contributor capacity exceeded for %s", g_Config[b].id);
        g_Boss[b].partial = true;
        return;
    }
    strcopy(row.identity, sizeof(row.identity), identity);
    strcopy(row.name, sizeof(row.name), name);
    row.damage = damage;
    rows.PushArray(row);
}

void StableSort(ArrayList rows) {
    Contribution value, previous;
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

void ClearResults() {
    if (g_Results == null) return;
    Result r;
    for (int i = 0; i < g_Results.Length; i++) { g_Results.GetArray(i, r); delete r.rows; }
    g_Results.Clear();
}

void QueueResult(int b) {
    Result r;
    strcopy(r.name, sizeof(r.name), g_Config[b].name);
    strcopy(r.unit, sizeof(r.unit), g_Config[b].unit);
    r.partial = g_Boss[b].partial;
    r.unknown = g_Boss[b].unknown;
    r.rows = g_Boss[b].contributions.Clone();
    StableSort(r.rows);
    r.hudDone = !cvRankHud.BoolValue;
    r.chatDone = !cvRankChat.BoolValue;
    // At most MAX_BOSSES fights can settle between resets, so this is a defensive fallback.
    // 两次重置之间最多结算 MAX_BOSSES 场战斗，这里只是防御性兜底。
    if (g_Results.Length >= MAX_RESULTS) {
        LogError("Ranking queue full: flushing oldest complete chat snapshot");
        Result oldest; g_Results.GetArray(0, oldest);
        if (!oldest.chatDone && cvRankChat.BoolValue)
            while (oldest.chatLine <= oldest.rows.Length + 1) SendResultLine(oldest);
        delete oldest.rows; g_Results.Erase(0);
    }
    // A newer fight replaces any ranking still on the HUD.
    // 新的战斗结果会替换 HUD 上仍在显示的排行。
    Result old;
    for (int i = 0; i < g_Results.Length; i++) {
        g_Results.GetArray(i, old); old.hudDone = true; g_Results.SetArray(i, old);
    }
    g_Results.PushArray(r);
}

void SurvivorChat(const char[] text) {
    g_Internal = true;
    for (int i = 1; i <= MaxClients; i++) if (Survivor(i) && !IsFakeClient(i)) PrintToChat(i, "%s", text);
    g_Internal = false;
}

void SendResultLine(Result r) {
    char text[256], name[100];
    if (r.chatLine == 0) {
        Utf8Copy(name, sizeof(name), r.name);
        Format(text, sizeof(text), "[MoreInfo] %s 已击败 · %s", name, r.unit);
    } else if (r.chatLine <= r.rows.Length) {
        Contribution row; r.rows.GetArray(r.chatLine-1, row);
        Utf8Copy(name, sizeof(name), row.name);
        Format(text, sizeof(text), "[MoreInfo] %d. %s  %.1f", r.chatLine, name, row.damage);
    } else {
        Format(text, sizeof(text), "[MoreInfo] 未归属: %.1f%s", r.unknown,
            r.partial ? " · 统计不完整（中途启用或适配异常）" : "");
    }
    SurvivorChat(text);
    r.chatLine++;
    if (r.chatLine > r.rows.Length + 1) r.chatDone = true;
}

void PumpRankings() {
    if (!cvRank.BoolValue) return;
    float now = GetGameTime();
    bool chatUsed = false, hudUsed = false;
    Result r;
    for (int i = 0; i < g_Results.Length; i++) {
        g_Results.GetArray(i, r);
        if (!r.chatDone && !chatUsed) {
            chatUsed = true;
            if (!cvRankChat.BoolValue) r.chatDone = true;
            else if (now >= r.nextChat) { SendResultLine(r); r.nextChat = now + cvChatTime.FloatValue; }
        }
        if (!r.hudDone && !hudUsed) {
            hudUsed = true;
            if (!cvRankHud.BoolValue) r.hudDone = true;
            else if (r.hudUntil == 0.0) {
                // Hold the ranking for rank_hud_time, cycling pages every rank_page_time.
                // 排行显示 rank_hud_time 秒，每 rank_page_time 秒翻一页。
                r.hudUntil = now + cvRankHudTime.FloatValue;
                r.nextHud = now + cvPageTime.FloatValue;
            } else if (now >= r.hudUntil) r.hudDone = true;
            else if (now >= r.nextHud) {
                int pages = (r.rows.Length + 2) / 3;
                if (pages < 1) pages = 1;
                r.hudPage = (r.hudPage + 1) % pages;
                r.nextHud = now + cvPageTime.FloatValue;
            }
        }
        g_Results.SetArray(i, r);
    }
    for (int i = g_Results.Length-1; i >= 0; i--) {
        g_Results.GetArray(i, r);
        if (r.chatDone && r.hudDone) { delete r.rows; g_Results.Erase(i); }
    }
}

void RankingText(char lines[7][512]) {
    if (!cvRank.BoolValue || !cvRankHud.BoolValue) return;
    Result r; bool found;
    for (int i = 0; i < g_Results.Length; i++) {
        g_Results.GetArray(i, r);
        if (!r.hudDone) { found = true; break; }
    }
    if (!found) return;
    int pages = (r.rows.Length + 2) / 3;
    if (pages < 1) pages = 1;
    char shortName[40]; Utf8Copy(shortName, sizeof(shortName), r.name);
    Format(lines[3], 512, "%s %d/%d%s 未归属:%.0f", shortName, r.hudPage+1, pages,
        r.partial ? " [不完整]" : "", r.unknown);
    for (int i = 0; i < 3; i++) {
        int index = r.hudPage * 3 + i;
        if (index >= r.rows.Length) break;
        Contribution row; r.rows.GetArray(index, row);
        Utf8Copy(shortName, sizeof(shortName), row.name);
        Format(lines[4+i], 512, "%d. %s  %.1f", index+1, shortName, row.damage);
    }
    if (!r.rows.Length) strcopy(lines[4], 512, "无可确认归属的玩家贡献");
}
