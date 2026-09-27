UserMsg g_SayText = INVALID_MESSAGE_ID, g_SayText2 = INVALID_MESSAGE_ID, g_TextMsg = INVALID_MESSAGE_ID;
// Listen-server host index (0 on dedicated). Map `say` via point_servercommand is
// attributed to the host there, so host chat is accepted but only by explicit rules.
// 本地服务器的房主索引（专用服务器为 0）。本地服务器上地图通过 point_servercommand 发出的 say
// 会被算在房主名下，所以房主的发言只有匹配明确规则时才接受。
int g_ListenHost, g_LastSender;
// Chat a player actually typed arrives through OnClientCommand first; map ServerCommand
// `say` does not. Same client + same tick + same text marks the say as player chat.
// 玩家亲手输入的聊天会先经过 OnClientCommand，地图 ServerCommand 的 say 不会。
// 同一客户端、同一 tick、同一文字，即判定为玩家聊天。
int g_TypedClient, g_TypedTick = -1;
char g_TypedText[512];

public Action OnClientCommand(int client, int args) {
    char command[16]; GetCmdArg(0, command, sizeof(command));
    if (StrEqual(command, "say", false) || StrEqual(command, "say_team", false)) {
        g_TypedClient = client; g_TypedTick = GetGameTickCount();
        GetCmdArgString(g_TypedText, sizeof(g_TypedText));
        StripQuotes(g_TypedText); CleanText(g_TypedText);
    }
    return Plugin_Continue;
}

bool TypedByPlayer(int client, const char[] source) {
    if (client <= 0 || client != g_TypedClient || GetGameTickCount() != g_TypedTick) return false;
    char text[512]; strcopy(text, sizeof(text), source);
    CleanText(text);
    return StrEqual(text, g_TypedText);
}

void HookMessages() {
    // Dedicated server console `say` is displayed through the player_chat event (userid 0).
    // 专用服务器控制台的 say 通过 player_chat 事件显示（userid 为 0）。
    if (!HookEventEx("player_chat", PlayerChat, EventHookMode_Pre)) LogError("player_chat event unavailable");
    if (GetUserMessageType() != UM_BitBuf) { LogError("Unsupported usermessage format: message HUD disabled"); return; }
    g_SayText = GetUserMessageId("SayText");
    g_SayText2 = GetUserMessageId("SayText2");
    g_TextMsg = GetUserMessageId("TextMsg");
    UserMsg ids[3]; ids[0] = g_SayText; ids[1] = g_SayText2; ids[2] = g_TextMsg;
    for (int i = 0; i < 3; i++) {
        if (ids[i] == INVALID_MESSAGE_ID) continue;
        HookUserMessage(ids[i], MessageHook, true);
    }
}

int ListenHost() {
    return !IsDedicatedServer() && IsClientInGame(1) && !IsFakeClient(1) ? 1 : 0;
}

bool MessageLive() {
    return g_MapReady && g_RoundLive && cvEnable.BoolValue && cvMsg.BoolValue && !g_Internal;
}

// Shared entry for server-originated text that is broadcast to everyone.
// Host-attributed text never uses loose mode: it must match an explicit rule.
// 服务器发出、广播给所有人的文字的统一入口。算在房主名下的文字不走宽松模式，必须匹配明确规则。
bool AcceptServerText(const char[] source, bool host) {
    char text[512]; strcopy(text, sizeof(text), source);
    CleanText(text);
    if (StrContains(text, "Console: ", false) == 0) { strcopy(text, sizeof(text), text[9]); TrimString(text); }
    if (!text[0] || text[0] == '#' || StrContains(text, "[MoreInfo]") != -1) return false;
    char key[64];
    if (!MatchMessageRules(text, key, sizeof(key), !host && cvFilter.IntValue == 1)) return false;
    int players[MAXPLAYERS+1], count;
    for (int c = 1; c <= MaxClients; c++) if (IsClientInGame(c)) players[count++] = c;
    StoreMessage(text, key, players, count);
    return true;
}

public Action ObserveSay(int client, const char[] command, int argc) {
    // Map text sent by point_servercommand `say` (e.g. ServerCommand -> say ** ... **).
    // Dedicated: client 0. Listen server: may be attributed to the host, so host needs a rule match.
    // 地图通过 point_servercommand 的 say 发出的文字（例如 ServerCommand -> say ** ... **）。
    // 专用服务器上 client 为 0；本地服务器上可能算在房主名下，所以房主需要匹配规则。
    char text[512]; GetCmdArgString(text, sizeof(text));
    StripQuotes(text);
    int host = ListenHost();
    if (TypedByPlayer(client, text) || (client != 0 && client != host) || !MessageLive()) return Plugin_Continue;
    return AcceptServerText(text, client != 0) && cvHideChat.BoolValue ? Plugin_Handled : Plugin_Continue;
}

public Action PlayerChat(Event event, const char[] name, bool dontBroadcast) {
    char text[512]; event.GetString("text", text, sizeof(text));
    int userid = event.GetInt("userid");
    if (!MessageLive() || event.GetBool("teamonly")) return Plugin_Continue;
    int client = userid ? GetClientOfUserId(userid) : 0;
    int host = ListenHost();
    if (userid && (!client || client != host || TypedByPlayer(client, text))) return Plugin_Continue; // ordinary player chat / 普通玩家聊天
    return AcceptServerText(text, client != 0) && cvHideChat.BoolValue ? Plugin_Handled : Plugin_Continue;
}

bool SharedRecipients(const int[] players, int count) {
    // The HUD is survivor-only. One recipient is safe when that player is the
    // only human survivor; MessageText still checks saved serials on every tick.
    // HUD 只对生还者显示。只有一个接收者时，若他是唯一的真人生还者也是安全的；
    // MessageText 每个 tick 仍会检查保存的序列号。
    int humans = 0;
    for (int c = 1; c <= MaxClients; c++) {
        if (!Survivor(c) || IsFakeClient(c)) continue;
        humans++;
        bool found;
        for (int i = 0; i < count; i++) if (players[i] == c) { found = true; break; }
        if (!found) return false;
    }
    return humans > 0;
}

bool DecodeMapMessage(UserMsg id, BfRead msg, char[] text, int size) {
    text[0] = '\0';
    g_LastSender = 0;
    char token[512], parameter[256];
    if (id == g_SayText) {
        if (msg.BytesLeft < 2) return false;
        g_LastSender = msg.ReadByte();
        if (g_LastSender != 0 && (!g_ListenHost || g_LastSender != g_ListenHost)) return false;
        msg.ReadString(text, size);
    } else if (id == g_SayText2) {
        if (msg.BytesLeft < 3) return false;
        g_LastSender = msg.ReadByte(); msg.ReadByte();
        if (g_LastSender != 0 && (!g_ListenHost || g_LastSender != g_ListenHost)) return false;
        msg.ReadString(token, sizeof(token));
        if (StrEqual(token, "#L4D_Chat_All")) {
            if (msg.BytesLeft > 0) msg.ReadString(parameter, sizeof(parameter)); // sender label / 发送者名称
            if (msg.BytesLeft > 0) msg.ReadString(text, size);
        } else if (token[0] != '#' && g_LastSender == 0) strcopy(text, size, token);
        else return false; // player/team/localized system tokens never promoted / 玩家、队伍和本地化系统消息不上 HUD
    } else if (id == g_TextMsg) {
        if (msg.BytesLeft < 2 || msg.ReadByte() != 3) return false; // HUD_PRINTTALK (chat area / 聊天区)
        msg.ReadString(token, sizeof(token));
        // These exact engine wrappers contain one literal chat parameter.
        // Do not expand arbitrary localization tokens or printf formats.
        // 这几个引擎包装格式只带一个原文聊天参数；不展开其他本地化标记或 printf 格式。
        if (StrEqual(token, "%s") || StrEqual(token, "%s1")) {
            if (msg.BytesLeft > 0) msg.ReadString(text, size);
        } else {
            if (token[0] == '#' || StrContains(token, "%s") != -1) return false;
            strcopy(text, size, token);
        }
    } else return false;
    CleanText(text);
    // Engine server say may carry a display label even though sender == 0.
    // 引擎的服务器 say 即使 sender 为 0 也可能带着显示前缀。
    if (StrContains(text, "Console: ", false) == 0) {
        strcopy(text, size, text[9]);
        TrimString(text);
    }
    return text[0] && text[0] != '#';
}

bool MatchMessageRules(const char[] text, char[] key, int size, bool loose) {
    key[0] = '\0';
    for (int i = 0; i < g_RuleCount; i++)
        if (g_Rules[i].exclude[0] && StrContains(text, g_Rules[i].exclude, false) != -1) return false;
    for (int i = 0; i < g_RuleCount; i++) {
        if (!g_Rules[i].prefix[0] && !g_Rules[i].contains[0]) continue;
        if (g_Rules[i].prefix[0] && StrContains(text, g_Rules[i].prefix, false) != 0) continue;
        if (g_Rules[i].contains[0] && StrContains(text, g_Rules[i].contains, false) == -1) continue;
        strcopy(key, size, g_Rules[i].key);
        return true;
    }
    return loose;
}

public Action MessageHook(UserMsg id, BfRead msg, const int[] players, int count, bool reliable, bool init) {
    if (!MessageLive()) return Plugin_Continue;
    g_ListenHost = ListenHost();
    char text[512];
    bool decoded = DecodeMapMessage(id, msg, text, sizeof(text));
    g_ListenHost = 0;
    if (!decoded || StrContains(text, "[MoreInfo]") != -1) return Plugin_Continue;
    bool host = g_LastSender != 0;
    if (host && TypedByPlayer(g_LastSender, text)) return Plugin_Continue; // the host's own chat / 房主自己的聊天
    char key[64];
    if (!SharedRecipients(players, count) ||
        !MatchMessageRules(text, key, sizeof(key), !host && cvFilter.IntValue == 1)) return Plugin_Continue;
    StoreMessage(text, key, players, count);
    return cvHideChat.BoolValue ? Plugin_Handled : Plugin_Continue;
}

bool WordAt(const char[] text, int pos, const char[] word) {
    for (int k = 0; word[k]; k++) if (!text[pos+k] || (text[pos+k] | 0x20) != word[k]) return false;
    return true;
}

// Seconds announced by the text itself ("in 30 seconds", "for 45 seconds", "2 minutes").
// The text is trusted as-is; 0 when no number is followed by a time unit.
// 从文字本身读出的秒数（"in 30 seconds"、"for 45 seconds"、"2 minutes"）。
// 直接信任文字内容；没有“数字 + 时间单位”时返回 0。
int ParseCountdown(const char[] text) {
    for (int i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') continue;
        int value = 0, j = i;
        while (text[j] >= '0' && text[j] <= '9') { if (value < 100000) value = value*10 + (text[j]-'0'); j++; }
        while (text[j] == ' ') j++;
        if (WordAt(text, j, "min")) value *= 60;
        else if (!WordAt(text, j, "sec")) { i = j-1; continue; }
        return value > 0 && value <= 3600 ? value : 0;
    }
    return 0;
}

void DisplayText(MapMessage row, float now, char[] text, int size) {
    if (row.deadline <= 0.0) { strcopy(text, size, row.shown); return; }
    int left = RoundToCeil(row.deadline - now);
    Format(text, size, "[%d秒] %s", left > 0 ? left : 0, row.shown);
}

int FoldAscii(int c) { return c >= 'A' && c <= 'Z' ? c | 0x20 : c; }

// Matches core text against a pattern (case-insensitive, runs of spaces equal one space).
// A {n} in the pattern matches one integer; its span is returned in numStart/numEnd.
// 用模板匹配核心文字（忽略大小写，连续空格视为一个）。模板里的 {n} 匹配一个整数，
// 其位置通过 numStart/numEnd 返回。
bool MatchPattern(const char[] text, int start, int end, const char[] pattern, int &numStart, int &numEnd) {
    int i = start, j = 0;
    numStart = numEnd = -1;
    while (pattern[j]) {
        if (IsPlaceholder(pattern, j)) {
            if (i >= end || text[i] < '0' || text[i] > '9') return false;
            numStart = i;
            while (i < end && text[i] >= '0' && text[i] <= '9') i++;
            numEnd = i; j += 3;
            continue;
        }
        if (pattern[j] == ' ') {
            if (i >= end || text[i] != ' ') return false;
            while (i < end && text[i] == ' ') i++;
            while (pattern[j] == ' ') j++;
            continue;
        }
        if (i >= end || FoldAscii(text[i]) != FoldAscii(pattern[j])) return false;
        i++; j++;
    }
    return i == end;
}

// Keeps the map's ** / *** decoration around the translated core. Returns false when no entry matches.
// 翻译核心文字，保留地图原有的 ** / *** 装饰。没有匹配的条目时返回 false。
bool TranslateText(const char[] text, char[] out, int size) {
    int len = strlen(text), start = 0, end = len;
    while (start < end && (text[start] == '*' || text[start] == ' ')) start++;
    while (end > start && (text[end-1] == '*' || text[end-1] == ' ')) end--;
    if (start >= end) return false;
    int numStart, numEnd;
    for (int t = 0; t < g_TrCount; t++) {
        if (!MatchPattern(text, start, end, g_Tr[t].en, numStart, numEnd)) continue;
        int o = 0;
        for (int k = 0; k < start && o < size-1; k++) out[o++] = text[k];
        for (int k = 0; g_Tr[t].zh[k] && o < size-1; ) {
            if (IsPlaceholder(g_Tr[t].zh, k)) {
                for (int d = numStart; d < numEnd && o < size-1; d++) out[o++] = text[d];
                k += 3;
            } else out[o++] = g_Tr[t].zh[k++];
        }
        for (int k = end; k < len && o < size-1; k++) out[o++] = text[k];
        out[o] = '\0';
        return true;
    }
    return false;
}

void StoreMessage(const char[] text, const char[] key, const int[] players, int count) {
    float now = GetGameTime();
    MapMessage row;
    for (int i = g_Messages.Length-1; i >= 0; i--) {
        g_Messages.GetArray(i, row);
        if (row.expires <= now) { g_Messages.Erase(i); continue; }
        if (StrEqual(row.text, text) && now < row.stored+0.3) return;
        if (key[0] && StrEqual(row.key, key)) g_Messages.Erase(i);
    }
    strcopy(row.text, sizeof(row.text), text); strcopy(row.key, sizeof(row.key), key);
    if (!cvTranslate.BoolValue || !TranslateText(text, row.shown, sizeof(row.shown))) {
        strcopy(row.shown, sizeof(row.shown), text);
        if (cvTranslate.BoolValue && g_TrCount && cvDebug.IntValue >= 1) LogMessage("Untranslated map message: %s", text);
    }
    row.stored = now;
    row.expires = now + cvMsgHold.FloatValue;
    int seconds = cvCountdown.BoolValue ? ParseCountdown(text) : 0;
    row.deadline = seconds ? now + float(seconds) : 0.0;
    // A countdown ignores map_msg_hold: visible until zero, then briefly shows 0.
    // 倒计时消息不受 map_msg_hold 限制：一直显示到归零，再短暂显示 0。
    if (seconds) row.expires = row.deadline + 2.0;
    if (g_Messages.Length >= 16) g_Messages.Erase(0);
    row.audienceCount = 0;
    for (int i = 0; i < count && row.audienceCount < MAXPLAYERS+1; i++)
        if (players[i] > 0 && players[i] <= MaxClients && IsClientInGame(players[i]))
            row.audience[row.audienceCount++] = GetClientSerial(players[i]);
    g_Messages.PushArray(row);
}

void MessageText(char lines[7][512]) {
    MapMessage row;
    float now = GetGameTime();
    for (int i = g_Messages.Length-1; i >= 0; i--) {
        g_Messages.GetArray(i, row);
        bool valid = row.expires > now;
        for (int c = 1; c <= MaxClients && valid; c++) {
            if (!Survivor(c) || IsFakeClient(c)) continue;
            bool found;
            for (int a = 0; a < row.audienceCount; a++) if (row.audience[a] == GetClientSerial(c)) found = true;
            if (!found) valid = false;
        }
        if (!valid) g_Messages.Erase(i);
    }
    if (!g_Messages.Length) return;
    char part[128], text[512];
    int newest = g_Messages.Length-1, timer = -1;
    for (int i = newest; i >= 0 && timer == -1; i--) {
        g_Messages.GetArray(i, row);
        if (row.deadline > 0.0) timer = i;
    }
    if (timer != -1 && timer != newest) {
        // Keep the running countdown on row 1 while newer notices use row 2.
        // 进行中的倒计时固定在第 1 行，更新的消息使用第 2 行。
        g_Messages.GetArray(timer, row); DisplayText(row, now, text, sizeof(text));
        Utf8Copy(part, sizeof(part), text); strcopy(lines[1], 512, part);
        g_Messages.GetArray(newest, row); DisplayText(row, now, text, sizeof(text));
        Utf8Copy(part, sizeof(part), text); strcopy(lines[2], 512, part);
        return;
    }
    g_Messages.GetArray(newest, row);
    DisplayText(row, now, text, sizeof(text));
    int next = Utf8Copy(part, sizeof(part), text);
    strcopy(lines[1], 512, part);
    if (text[next]) {
        // Rotate pairs of UTF-8 chunks to expose long messages within the hold period.
        // 长消息按 UTF-8 分段，两段一组轮播，在显示时间内展示完整内容。
        int starts[8], chunks = 0, pos = 0;
        while (text[pos] && chunks < sizeof(starts)) {
            starts[chunks++] = pos; pos = Utf8Copy(part, sizeof(part), text, pos);
        }
        int pair = RoundToFloor((now-row.stored)/1.5) % ((chunks+1)/2);
        Utf8Copy(part, sizeof(part), text, starts[pair*2]); strcopy(lines[1], 512, part);
        if (pair*2+1 < chunks) { Utf8Copy(part, sizeof(part), text, starts[pair*2+1]); strcopy(lines[2], 512, part); }
    } else if (g_Messages.Length > 1) {
        strcopy(lines[2], 512, lines[1]);
        g_Messages.GetArray(newest-1, row); DisplayText(row, now, text, sizeof(text));
        Utf8Copy(part, sizeof(part), text); strcopy(lines[1], 512, part);
    }
}
