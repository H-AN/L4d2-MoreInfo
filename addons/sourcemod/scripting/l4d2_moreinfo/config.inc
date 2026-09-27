bool ParseSlots(const char[] text, int[] slots, int count) {
    char parts[16][16];
    if (ExplodeString(text, ",", parts, sizeof(parts), sizeof(parts[])) != count) return false;
    for (int i = 0; i < count; i++) {
        TrimString(parts[i]);
        int n = strlen(parts[i]);
        if (!n) return false;
        for (int c = 0; c < n; c++) if (parts[i][c] < '0' || parts[i][c] > '9') return false;
        slots[i] = StringToInt(parts[i]);
        if (slots[i] < 0 || slots[i] >= 15) return false;
    }
    return true;
}

bool ReadSlots(int slots[7]) {
    char text[128];
    int one[1], two[2], four[4];
    cvBossSlot.GetString(text, sizeof(text));
    if (!ParseSlots(text, one, 1)) return false;
    cvMsgSlots.GetString(text, sizeof(text));
    if (!ParseSlots(text, two, 2)) return false;
    cvRankSlots.GetString(text, sizeof(text));
    if (!ParseSlots(text, four, 4)) return false;
    slots[0] = one[0];
    for (int i = 0; i < 2; i++) slots[i+1] = two[i];
    for (int i = 0; i < 4; i++) slots[i+3] = four[i];
    bool used[15];
    for (int i = 0; i < 7; i++) {
        if (used[slots[i]] || g_Reserved[slots[i]]) return false;
        used[slots[i]] = true;
    }
    return true;
}

bool ReadSelector(KeyValues kv, Selector selector) {
    kv.GetString("classname", selector.classname, sizeof(selector.classname));
    kv.GetString("targetname", selector.targetname, sizeof(selector.targetname));
    selector.hammer = kv.GetNum("hammer_id", 0);
    return selector.classname[0] && (selector.targetname[0] || selector.hammer > 0);
}

bool ReadSignal(KeyValues kv, const char[] key, Signal signal, bool required = false) {
    signal.output[0] = '\0';
    if (!kv.JumpToKey(key)) return !required;
    bool valid = ReadSelector(kv, signal.entity);
    kv.GetString("output", signal.output, sizeof(signal.output));
    kv.GoBack();
    return valid && signal.output[0] != '\0';
}

bool ParsePhase(KeyValues kv, PhaseConfig phase) {
    char model[32], attribution[32];
    kv.GetString("model", model, sizeof(model), "health");
    if (StrEqual(model, "health")) phase.model = 0;
    else if (StrEqual(model, "counter_down")) phase.model = 1;
    else if (StrEqual(model, "counter_up")) phase.model = 2;
    else return false;
    phase.minimum = kv.GetFloat("counter_min", 0.0);
    phase.maximum = kv.GetFloat("counter_max", 0.0);
    phase.fixedMax = kv.GetFloat("max_hp", 0.0);
    phase.captureAtStart = kv.GetNum("capture_max_on_start", 0) != 0;
    phase.startDelay = kv.GetFloat("start_delay", 0.0);
    kv.GetString("attribution", attribution, sizeof(attribution), "unknown");
    if (!StrEqual(attribution, "unknown") && !StrEqual(attribution, "verified_sync")) return false;
    phase.synchronous = StrEqual(attribution, "verified_sync");
    if (phase.fixedMax < 0.0 || phase.fixedMax >= 1000000000.0 ||
        (phase.model == 2 && phase.maximum <= phase.minimum)) return false;
    if (!kv.JumpToKey("health")) return false;
    bool valid = ReadSelector(kv, phase.health);
    kv.GoBack();
    if (!valid || (phase.model != 0 && !StrEqual(phase.health.classname, "math_counter"))) return false;
    if (!ReadSignal(kv, "start", phase.start) || !ReadSignal(kv, "finish", phase.finish, true)) return false;
    if (phase.startDelay < 0.0 || phase.startDelay > 60.0 ||
        ((phase.captureAtStart || phase.startDelay > 0.0) && !phase.start.output[0])) return false;
    phase.hitCount = 0;
    if (kv.JumpToKey("hitboxes")) {
        if (kv.GotoFirstSubKey()) {
            do {
                if (phase.hitCount >= MAX_HITS || !ReadSelector(kv, phase.hits[phase.hitCount])) return false;
                phase.hitCount++;
            } while (kv.GotoNextKey());
            kv.GoBack();
        }
        kv.GoBack();
    }
    if (phase.hitCount == 0 && phase.model == 0) {
        phase.hits[0] = phase.health;
        phase.hitCount = 1;
    }
    return phase.hitCount > 0;
}

bool ParseMap(KeyValues kv, BossConfig[] bosses, int &count, MessageRule[] rules, int &ruleCount) {
    count = 0; ruleCount = 0;
    if (kv.JumpToKey("bosses")) {
        if (kv.GotoFirstSubKey()) {
            do {
                if (count >= MAX_BOSSES) return false;
                kv.GetSectionName(bosses[count].id, sizeof(bosses[].id));
                for (int j = 0; j < count; j++) if (StrEqual(bosses[j].id, bosses[count].id)) return false;
                kv.GetString("name", bosses[count].name, sizeof(bosses[].name), bosses[count].id);
                kv.GetString("unit", bosses[count].unit, sizeof(bosses[].unit), "有效伤害");
                if (!ReadSignal(kv, "cancel", bosses[count].cancel)) return false;
                if (!kv.JumpToKey("phases") || !kv.GotoFirstSubKey()) return false;
                int p = 0;
                do {
                    if (p >= MAX_PHASES || !ParsePhase(kv, bosses[count].phases[p])) return false;
                    p++;
                } while (kv.GotoNextKey());
                kv.GoBack(); kv.GoBack();
                bosses[count].phaseCount = p;
                count++;
            } while (kv.GotoNextKey());
            kv.GoBack();
        }
        kv.GoBack();
    }
    if (kv.JumpToKey("messages")) {
        if (kv.GotoFirstSubKey()) {
            do {
                if (ruleCount >= MAX_RULES) return false;
                kv.GetString("prefix", rules[ruleCount].prefix, sizeof(rules[].prefix));
                kv.GetString("contains", rules[ruleCount].contains, sizeof(rules[].contains));
                kv.GetString("exclude", rules[ruleCount].exclude, sizeof(rules[].exclude));
                kv.GetString("key", rules[ruleCount].key, sizeof(rules[].key));
                if (!rules[ruleCount].prefix[0] && !rules[ruleCount].contains[0] && !rules[ruleCount].exclude[0]) return false;
                ruleCount++;
            } while (kv.GotoNextKey());
            kv.GoBack();
        }
        kv.GoBack();
    }
    return true;
}

bool IsPlaceholder(const char[] text, int pos) {
    return text[pos] == '{' && text[pos+1] == 'n' && text[pos+2] == '}';
}

int CountPlaceholders(const char[] text) {
    int n = 0;
    for (int i = 0; text[i]; i++) if (IsPlaceholder(text, i)) n++;
    return n;
}

// "Translations" { "<id>" { "en" "Doors will open in {n} seconds" "zh" "{n} 秒后开门" } }
// en is matched without the surrounding ** / *** decoration, ignoring case and repeated spaces.
// en 匹配时去掉两侧的 ** / *** 装饰，忽略大小写，连续空格视为一个。
bool ParseTranslations(KeyValues kv, MessageTranslation[] out, int &count) {
    count = 0;
    if (!kv.GotoFirstSubKey()) return true;
    do {
        if (count >= MAX_TRANSLATIONS) return false;
        kv.GetString("en", out[count].en, sizeof(out[].en));
        kv.GetString("zh", out[count].zh, sizeof(out[].zh));
        int en = CountPlaceholders(out[count].en), zh = CountPlaceholders(out[count].zh);
        if (!out[count].en[0] || !out[count].zh[0] || en > 1 || zh > en) return false;
        count++;
    } while (kv.GotoNextKey());
    kv.GoBack();
    return true;
}

// Per-map file: configs/l4d2_moreinfo/maps/<map>/<file>.
// 每张地图的配置文件：configs/l4d2_moreinfo/maps/<地图名>/<文件名>。
void MapFile(const char[] map, const char[] file, char[] path, int size) {
    BuildPath(Path_SM, path, size, "configs/l4d2_moreinfo/maps/%s/%s", map, file);
}

bool SameSelector(Selector a, Selector b) {
    return StrEqual(a.classname, b.classname) && StrEqual(a.targetname, b.targetname) && a.hammer == b.hammer;
}
bool SameSignal(Signal a, Signal b) { return StrEqual(a.output, b.output) && (!a.output[0] || SameSelector(a.entity, b.entity)); }
bool SameMechanics(BossConfig a, BossConfig b) {
    if (!StrEqual(a.id, b.id) || !StrEqual(a.unit, b.unit) || a.phaseCount != b.phaseCount || !SameSignal(a.cancel, b.cancel)) return false;
    for (int p = 0; p < a.phaseCount; p++) {
        if (!SameSelector(a.phases[p].health, b.phases[p].health) || a.phases[p].model != b.phases[p].model ||
            a.phases[p].minimum != b.phases[p].minimum || a.phases[p].maximum != b.phases[p].maximum ||
            a.phases[p].fixedMax != b.phases[p].fixedMax || a.phases[p].captureAtStart != b.phases[p].captureAtStart ||
            a.phases[p].startDelay != b.phases[p].startDelay ||
            a.phases[p].synchronous != b.phases[p].synchronous || a.phases[p].hitCount != b.phases[p].hitCount ||
            !SameSignal(a.phases[p].start, b.phases[p].start) || !SameSignal(a.phases[p].finish, b.phases[p].finish)) return false;
        for (int h = 0; h < a.phases[p].hitCount; h++) if (!SameSelector(a.phases[p].hits[h], b.phases[p].hits[h])) return false;
    }
    return true;
}

bool LoadLayout(float rect[7][4], bool reserved[15]) {
    char path[PLATFORM_MAX_PATH];
    BuildPath(Path_SM, path, sizeof(path), "configs/l4d2_moreinfo/hud.cfg");
    KeyValues kv = new KeyValues("MoreInfoHUD");
    if (!kv.ImportFromFile(path)) { delete kv; return false; }
    char list[128];
    kv.GetString("reserved_slots", list, sizeof(list));
    if (list[0]) {
        char parts[16][16]; int slots[15];
        int n = ExplodeString(list, ",", parts, sizeof(parts), sizeof(parts[]));
        if (n > 15 || !ParseSlots(list, slots, n)) { delete kv; return false; }
        for (int i = 0; i < n; i++) reserved[slots[i]] = true;
    }
    char sections[][] = {"boss", "message_1", "message_2", "rank_title", "rank_1", "rank_2", "rank_3"};
    for (int i = 0; i < 7; i++) {
        if (!kv.JumpToKey(sections[i])) { delete kv; return false; }
        rect[i][0] = kv.GetFloat("x", -1.0); rect[i][1] = kv.GetFloat("y", -1.0);
        rect[i][2] = kv.GetFloat("width", -1.0); rect[i][3] = kv.GetFloat("height", -1.0);
        kv.GoBack();
        if (rect[i][0] < 0.0 || rect[i][1] < 0.0 || rect[i][2] <= 0.0 || rect[i][3] <= 0.0 ||
            rect[i][0]+rect[i][2] > 1.001 || rect[i][1]+rect[i][3] > 1.001) { delete kv; return false; }
        for (int j = 0; j < i; j++) {
            if (rect[i][0] < rect[j][0]+rect[j][2] && rect[j][0] < rect[i][0]+rect[i][2] &&
                rect[i][1] < rect[j][1]+rect[j][3] && rect[j][1] < rect[i][1]+rect[i][3]) { delete kv; return false; }
        }
        if (reserved[g_Slots[i]]) { delete kv; return false; }
    }
    delete kv;
    return true;
}

bool LoadConfiguration(bool reload, char[] error, int size) {
    // Stage the entire transaction. No live state is touched before validation finishes.
    // 先完整解析到临时变量，校验全部通过前不改动任何运行中的状态。
    static BossConfig next[MAX_BOSSES];
    static MessageRule rules[MAX_RULES];
    BossConfig empty; MessageRule emptyRule;
    for (int i = 0; i < MAX_BOSSES; i++) next[i] = empty;
    for (int i = 0; i < MAX_RULES; i++) rules[i] = emptyRule;
    float layout[7][4]; bool reserved[15];
    if (!LoadLayout(layout, reserved)) { strcopy(error, size, "Invalid/missing hud.cfg, overlapping rectangles or reserved slot"); return false; }
    char map[128], path[PLATFORM_MAX_PATH];
    GetCurrentMap(map, sizeof(map));
    ReplaceString(map, sizeof(map), "/", "_"); ReplaceString(map, sizeof(map), "\\", "_");
    MapFile(map, "config.cfg", path, sizeof(path));
    int count, ruleCount;
    KeyValues kv = new KeyValues("MoreInfo");
    if (FileExists(path)) {
        if (!kv.ImportFromFile(path) || !ParseMap(kv, next, count, rules, ruleCount)) {
            delete kv; strcopy(error, size, "Invalid map configuration (limits/selectors/phases/signals)"); return false;
        }
    }
    delete kv;
    bool mechanicsChanged = count != g_BossCount;
    if (!mechanicsChanged) for (int i = 0; i < count; i++) if (!SameMechanics(next[i], g_Config[i])) mechanicsChanged = true;
    ReleaseHud();
    for (int i = 0; i < 7; i++) for (int j = 0; j < 4; j++) g_Layout[i][j] = layout[i][j];
    for (int i = 0; i < 15; i++) { g_Reserved[i] = reserved[i]; g_Hud[i].blocked = false; }
    if (mechanicsChanged || !reload) {
        if (reload) LogMessage("Map mechanics changed: cancelling old fights; new statistics are partial");
        g_Epoch++;
        ResetRuntime(reload || g_Late);
    }
    g_BossCount = count; g_RuleCount = ruleCount;
    for (int i = 0; i < count; i++) g_Config[i] = next[i];
    for (int i = 0; i < ruleCount; i++) g_Rules[i] = rules[i];
    // Translations only affect HUD text; a broken file logs and falls back to original text.
    // 翻译只影响 HUD 文字；文件有误时记录错误并显示原文。
    g_TrCount = 0;
    MapFile(map, "translations.cfg", path, sizeof(path));
    if (FileExists(path)) {
        kv = new KeyValues("Translations");
        if (!kv.ImportFromFile(path) || !ParseTranslations(kv, g_Tr, g_TrCount)) {
            g_TrCount = 0;
            LogError("Invalid %s (en/zh required, at most one {n}, max %d entries); showing original text", path, MAX_TRANSLATIONS);
        }
        delete kv;
    }
    UnregisterSignals();
    RegisterSignals();
    if (g_MapReady && cvEnable.BoolValue) ScanBindings();
    LogMessage("Loaded map=%s bosses=%d message_rules=%d translations=%d", map, count, ruleCount, g_TrCount);
    return true;
}
