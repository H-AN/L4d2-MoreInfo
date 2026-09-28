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
    for (int i = 0; i < g_Results.Length; i++) { g_Results.GetArray(i, r); delete r.rows; delete r.materia; }
    g_Results.Clear();
}

// Chat lines: header, contributors, unknown; then (only with materia) a materia header and rows.
// 聊天行：标题、贡献者、未归属；有神器伤害时再加神器标题和每一行。
int ResultLastLine(Result r) {
    int last = r.rows.Length + 1;
    if (r.materia.Length) last += r.materia.Length + 1;
    return last;
}

// "[终极]" or "[终极]×2" when cast more than once. / 施放多次时显示“[终极]×2”。
void MateriaLabel(MateriaRow row, char[] text, int size) {
    if (row.casts > 1) Format(text, size, "[%s]×%d", row.label, row.casts);
    else Format(text, size, "[%s]", row.label);
}

void QueueResult(int b) {
    Result r;
    strcopy(r.name, sizeof(r.name), g_Config[b].name);
    strcopy(r.unit, sizeof(r.unit), g_Config[b].unit);
    r.partial = g_Boss[b].partial;
    r.unknown = g_Boss[b].unknown;
    r.rows = g_Boss[b].contributions.Clone();
    StableSort(r.rows);
    r.materia = g_Boss[b].materia.Clone();
    SortMateria(r.materia);
    r.hudDone = !cvRankHud.BoolValue;
    r.chatDone = !cvRankChat.BoolValue;
    // At most MAX_BOSSES fights can settle between resets, so this is a defensive fallback.
    // 两次重置之间最多结算 MAX_BOSSES 场战斗，这里只是防御性兜底。
    if (g_Results.Length >= MAX_RESULTS) {
        LogError("Ranking queue full: flushing oldest complete chat snapshot");
        Result oldest; g_Results.GetArray(0, oldest);
        if (!oldest.chatDone && cvRankChat.BoolValue)
            while (oldest.chatLine <= ResultLastLine(oldest)) SendResultLine(oldest);
        delete oldest.rows; delete oldest.materia; g_Results.Erase(0);
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
    } else if (r.chatLine == r.rows.Length + 1) {
        Format(text, sizeof(text), "[MoreInfo] 未归属: %.1f%s", r.unknown,
            r.partial ? " · 统计不完整（中途启用或适配异常）" : "");
    } else if (r.chatLine == r.rows.Length + 2) {
        Utf8Copy(name, sizeof(name), r.name);
        Format(text, sizeof(text), "[MoreInfo] %s 神器伤害（已计入上方总伤害）", name);
    } else {
        int index = r.chatLine - r.rows.Length - 3;
        MateriaRow row; r.materia.GetArray(index, row);
        char label[48]; MateriaLabel(row, label, sizeof(label));
        Utf8Copy(name, sizeof(name), row.name);
        Format(text, sizeof(text), "[MoreInfo] [神器] %d. %s %s  %.1f", index+1, name, label, row.damage);
    }
    SurvivorChat(text);
    r.chatLine++;
    if (r.chatLine > ResultLastLine(r)) r.chatDone = true;
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
            // A live materia notice owns the region; the ranking timer starts after it.
            // 实时神器提示占用该区域时，排行计时等提示结束后再开始。
            else if (r.hudUntil == 0.0 && NoticeActive()) {}
            else if (r.hudUntil == 0.0) {
                // Hold the ranking for rank_hud_time, cycling pages every rank_page_time.
                // 排行显示 rank_hud_time 秒，每 rank_page_time 秒翻一页。
                r.hudUntil = now + cvRankHudTime.FloatValue;
                r.nextHud = now + cvPageTime.FloatValue;
            } else if (now >= r.hudUntil) r.hudDone = true;
            else if (now >= r.nextHud) {
                r.hudPage = (r.hudPage + 1) % ResultPages(r);
                r.nextHud = now + cvPageTime.FloatValue;
            }
        }
        g_Results.SetArray(i, r);
    }
    for (int i = g_Results.Length-1; i >= 0; i--) {
        g_Results.GetArray(i, r);
        if (r.chatDone && r.hudDone) { delete r.rows; delete r.materia; g_Results.Erase(i); }
    }
}

// Total pages first (at least one), then materia pages; paging loops over both.
// 先是总伤害页（至少一页），然后是神器伤害页；翻页在两者之间循环。
int RankPages(Result r) {
    int pages = (r.rows.Length + 2) / 3;
    return pages < 1 ? 1 : pages;
}
int ResultPages(Result r) { return RankPages(r) + (r.materia.Length + 2) / 3; }

void RankingText(char lines[7][512]) {
    if (!cvRank.BoolValue || !cvRankHud.BoolValue) return;
    Result r; bool found;
    for (int i = 0; i < g_Results.Length; i++) {
        g_Results.GetArray(i, r);
        if (!r.hudDone) { found = true; break; }
    }
    if (!found) return;
    int rankPages = RankPages(r), pages = ResultPages(r);
    char shortName[40]; Utf8Copy(shortName, sizeof(shortName), r.name);
    if (r.hudPage >= rankPages) {
        Format(lines[3], 512, "%s 神器伤害 %d/%d", shortName, r.hudPage+1, pages);
        for (int i = 0; i < 3; i++) {
            int index = (r.hudPage - rankPages) * 3 + i;
            if (index >= r.materia.Length) break;
            MateriaRow row; r.materia.GetArray(index, row);
            char label[48]; MateriaLabel(row, label, sizeof(label));
            Utf8Copy(shortName, sizeof(shortName), row.name);
            Format(lines[4+i], 512, "%d. %s %s  %.1f", index+1, shortName, label, row.damage);
        }
        return;
    }
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
