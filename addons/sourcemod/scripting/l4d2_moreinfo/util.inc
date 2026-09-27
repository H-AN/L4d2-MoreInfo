// UTF-8 byte budgets include the terminating zero. Never emit a partial codepoint.
// UTF-8 字节预算包含结尾的 0，绝不截断半个字符。
int Utf8Copy(char[] dest, int capacity, const char[] source, int start = 0) {
    int written = 0, length = strlen(source);
    while (start < length) {
        int byte = source[start] & 255;
        int count = byte < 128 ? 1 : ((byte & 224) == 192 ? 2 : ((byte & 240) == 224 ? 3 : 4));
        if (start + count > length || written + count >= capacity) break;
        for (int n = 0; n < count; n++) dest[written++] = source[start++];
    }
    dest[written] = '\0';
    return start;
}

float Positive(float value) { return value > 0.0 ? value : 0.0; }

void CleanText(char[] text) {
    int j = 0;
    for (int i = 0; text[i]; i++) {
        int c = text[i] & 255;
        if (c >= 32 && c != 127) text[j++] = text[i];
        else if (c == 10 || c == 13 || c == 9) text[j++] = ' ';
    }
    text[j] = '\0';
    TrimString(text);
}

bool Survivor(int client) {
    return client > 0 && client <= MaxClients && IsClientInGame(client) && GetClientTeam(client) == 2;
}

void PlayerIdentity(int client, char[] identity, int size, char[] name, int nameSize) {
    identity[0] = '\0'; name[0] = '\0';
    if (!Survivor(client)) return;
    if (IsFakeClient(client) || !GetClientAuthId(client, AuthId_SteamID64, identity, size, true))
        Format(identity, size, "session:%d", GetClientSerial(client));
    GetClientName(client, name, nameSize);
    CleanText(name);
}

// Non-networked entities (point_template, logic_*) arrive as negative entity references
// from output hooks and FindEntityByClassname; natives accept them directly.
// 非网络实体（point_template、logic_* 等）在输出钩子和 FindEntityByClassname 中以负数引用出现，
// 原生函数可以直接使用。
bool NotEntity(int entity) {
    return entity == INVALID_ENT_REFERENCE || (entity >= 0 && entity <= MaxClients) || !IsValidEntity(entity);
}

bool Matches(int entity, Selector selector) {
    if (NotEntity(entity) || !selector.classname[0]) return false;
    char classname[64], targetname[128];
    GetEntityClassname(entity, classname, sizeof(classname));
    if (!StrEqual(classname, selector.classname)) return false;
    if (selector.targetname[0]) {
        if (!HasEntProp(entity, Prop_Data, "m_iName")) return false;
        GetEntPropString(entity, Prop_Data, "m_iName", targetname, sizeof(targetname));
        if (!StrEqual(targetname, selector.targetname)) return false;
    }
    return selector.hammer <= 0 || (HasEntProp(entity, Prop_Data, "m_iHammerID")
        && GetEntProp(entity, Prop_Data, "m_iHammerID") == selector.hammer);
}

int FindUnique(Selector selector) {
    // FindEntityByClassname also visits non-networked entities, returned as references.
    // FindEntityByClassname 也会遍历非网络实体，返回的是引用。
    int result = -1, e = -1;
    while ((e = FindEntityByClassname(e, selector.classname)) != -1) {
        if (!Matches(e, selector)) continue;
        if (result != -1) return -2;
        result = e;
    }
    return result;
}

bool ReadHealth(int entity, int model, float minimum, float maximum, float &hp) {
    if (NotEntity(entity)) return false;
    if (model == 0) {
        if (!HasEntProp(entity, Prop_Data, "m_iHealth")) return false;
        hp = Positive(float(GetEntProp(entity, Prop_Data, "m_iHealth")));
    } else {
        // m_OutValue is an embedded COutputFloat. Its variant's float is at offset zero.
        // m_OutValue 是内嵌的 COutputFloat，其中的浮点值位于偏移 0。
        int offset = FindDataMapInfo(entity, "m_OutValue");
        if (offset <= 0) return false;
        float value = GetEntDataFloat(entity, offset);
        hp = Positive(model == 1 ? value - minimum : maximum - value);
    }
    return hp == hp && hp < 1000000000.0;
}

bool CurrentHealth(int b, float &hp) {
    int p = g_Boss[b].phase;
    return ReadHealth(g_Boss[b].healthRef, g_Config[b].phases[p].model,
        g_Config[b].phases[p].minimum, g_Config[b].phases[p].maximum, hp);
}
