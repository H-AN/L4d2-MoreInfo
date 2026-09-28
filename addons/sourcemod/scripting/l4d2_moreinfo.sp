#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <sdkhooks>

#define VERSION "1.0.4"
#define MAX_BOSSES 16
#define MAX_PHASES 4
#define MAX_HITS 8
#define MAX_RULES 32
#define MAX_TRANSLATIONS 96
#define MAX_ENTS 2049
#define MAX_CONTEXTS 32
#define MAX_RESULTS 32
#define MAX_CONTRIBUTORS 256
#define MAX_CREDITS 16
#define MAX_APPLIES 4
#define MAX_PENDING 32
#define MAX_NOTICES 4
#define CAST_WINDOW 3.0   // apply outputs must follow the caster output within this time / 生效输出必须在施放输出后这段时间内触发
#define CREDIT_WINDOW 1.0 // seconds after the configured landing time a health drop is still credited / 预计生效后多少秒内的掉血仍算作该次施放
#define HUD_FLAGS ((1<<6)|(1<<8)|(1<<10)|(1<<13))
#define HUD_HIDDEN (1<<14)

public Plugin myinfo = {
    name = "L4D2 MoreInfo", 
    author = "H-AN",
    description = "Map boss health, map messages and complete contribution rankings",
    version = VERSION, 
    url = "QQ群107866133, github https://github.com/H-AN"
};

enum struct Selector {
    char classname[64];
    char targetname[128];
    int hammer;
}
enum struct Signal {
    Selector entity;
    char output[64];
}
enum struct PhaseConfig {
    Selector health;
    Selector hits[MAX_HITS];
    int hitCount;
    int model; // 0 entity health, 1 decreasing counter, 2 increasing counter / 0 实体血量，1 递减计数器，2 递增计数器
    float minimum;
    float maximum;
    float fixedMax;
    bool captureAtStart;
    float startDelay; // wait for delayed map I/O before sampling initialized HP / 等地图延迟 I/O 完成后再采样初始血量
    bool synchronous; // explicitly verified immediate I/O, otherwise unknown attribution / 已确认伤害即时生效，否则计入未归属
    Signal start;
    Signal finish;
}
enum struct BossConfig {
    char id[64];
    char name[96];
    char unit[32];
    int phaseCount;
    PhaseConfig phases[MAX_PHASES];
    Signal cancel;
}
enum struct Contribution {
    char identity[64];
    char name[128];
    float damage;
}
enum struct BossState {
    int serial;
    int phase;
    int healthRef;
    int status; // 0 waiting, 1 active, 2 pending finish, 3 settled, 4 cancelled / 0 等待，1 进行中，2 结束中，3 已结算，4 已取消
    float hp;
    float maxHp;
    float lastHit;
    float unknown;
    bool partial;
    bool bots;
    bool ambiguous;
    bool startSeen;
    bool startPending;
    bool startDamaged;
    float startAt;
    ArrayList contributions;
    ArrayList materia; // MateriaRow: credited scripted damage per player and label / 每位玩家每种脚本伤害（神器）的统计
}
// Scripted damage (e.g. materia) credited to the player who fired the caster output.
// 地图脚本伤害（如神器）归属给触发施放输出的玩家。
enum struct CreditConfig {
    int boss;
    char label[32];
    Signal caster;
    Signal applies[MAX_APPLIES];
    float amounts[MAX_APPLIES];
    float delays[MAX_APPLIES]; // action delay between the apply output and the health change / 生效输出到实际扣血之间的延迟
    int applyCount;
}
enum struct PendingCredit {
    int boss;
    int serial;
    int credit;
    int cast;
    char identity[64];
    char name[128];
    float remaining;
    float credited;
    float activeFrom;
    float activeUntil;
}
enum struct MateriaRow {
    char identity[64];
    char name[128];
    char label[32];
    float damage;
    int casts;
    int lastCast;
}
enum struct CreditNotice {
    char text[128];
    float expires;
}
enum struct DamageContext {
    int victim;
    int entityRef;
    int boss;
    int serial;
    int phase;
    int healthRef;
    float before;
    float nested;
    bool uncertain;
    char identity[64];
    char name[128];
}
enum struct MessageRule {
    char prefix[128];
    char contains[128];
    char exclude[128];
    char key[64];
}
enum struct MessageTranslation {
    char en[160]; // may contain one {n} matching an integer / 可包含一个匹配整数的 {n}
    char zh[256];
}
enum struct MapMessage {
    char text[512];  // original map text: rules, dedupe and countdown use this / 地图原文，用于规则、去重和倒计时
    char shown[512]; // translated (or original) text for the HUD / HUD 上显示的译文（或原文）
    char key[64];
    float stored;
    float deadline; // 0 = no countdown; otherwise game time the announced "N seconds" ends / 0 表示无倒计时，否则为“N 秒”结束时的游戏时间
    float expires;
    int audience[MAXPLAYERS+1];
    int audienceCount;
}
enum struct Result {
    char name[96];
    char unit[32];
    float unknown;
    bool partial;
    ArrayList rows;
    ArrayList materia;
    int hudPage;
    int chatLine;
    float nextHud;
    float hudUntil;
    float nextChat;
    bool hudDone;
    bool chatDone;
}
enum struct HudSlot {
    bool owned;
    bool blocked;
    char text[128];
    float rect[4];
}

BossConfig g_Config[MAX_BOSSES];
BossState g_Boss[MAX_BOSSES];
MessageRule g_Rules[MAX_RULES];
int g_BossCount, g_RuleCount;
MessageTranslation g_Tr[MAX_TRANSLATIONS];
int g_TrCount;
DamageContext g_Context[MAX_CONTEXTS];
int g_Depth, g_Overflow;
int g_EntityBoss[MAX_ENTS], g_EntityRef[MAX_ENTS];
bool g_Hooked[MAX_ENTS];
ArrayList g_Messages, g_Results;
StringMap g_OutputHooks;
CreditConfig g_Credit[MAX_CREDITS];
int g_CreditCount;
ArrayList g_Pending, g_Notices;
StringMap g_CreditHooks;
char g_CastIdentity[MAX_CREDITS][64], g_CastName[MAX_CREDITS][128];
float g_CastAt[MAX_CREDITS];
int g_CastId[MAX_CREDITS], g_CastSerial;
HudSlot g_Hud[15];
int g_Slots[7] = {0, 1, 2, 3, 4, 5, 6};
float g_Layout[7][4];
bool g_Reserved[15];
bool g_HudReady, g_MapReady, g_RoundLive, g_Late, g_Internal, g_Reverting, g_ScanQueued;
int g_Epoch, g_Serial;
float g_NextHud, g_HudTestUntil, g_HudStompLog;
int g_HudStomps;
int g_HudBackend = 1; // applied hud_backend: 0 netprop, 1 VScript (smHud.inc) / 当前生效的 HUD 后端
bool g_HudDirty;      // VScript fields queued, HUDSetLayout pending / 已写入 VScript 字段，待调用 HUDSetLayout
float g_HudSent[15];
ConVar cvEnable, cvHp, cvMsg, cvRank, cvRankHud, cvRankChat;
ConVar cvBossSlot, cvMsgSlots, cvRankSlots, cvInterval, cvHpHold, cvMsgHold;
ConVar cvFilter, cvPageTime, cvChatTime, cvBots, cvDebug, cvHideChat, cvRankHudTime, cvCountdown, cvTranslate;
ConVar cvNoticeHud, cvNoticeChat, cvNoticeHold, cvHudFrame, cvHudBackend;

#include "l4d2_moreinfo/smHud.inc"
#include "l4d2_moreinfo/util.inc"
#include "l4d2_moreinfo/config.inc"
#include "l4d2_moreinfo/hud.inc"
#include "l4d2_moreinfo/ranking.inc"
#include "l4d2_moreinfo/boss.inc"
#include "l4d2_moreinfo/credit.inc"
#include "l4d2_moreinfo/messages.inc"
#include "l4d2_moreinfo/admin.inc"

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int errMax) {
    g_Late = late;
    return APLRes_Success;
}

ConVar Setting(const char[] suffix, const char[] value, const char[] description,
               bool bounded = false, float minimum = 0.0, float maximum = 1.0) {
    char name[96];
    Format(name, sizeof(name), "l4d2_moreinfo_%s", suffix);
    ConVar cvar = CreateConVar(name, value, description, FCVAR_NONE, bounded, minimum, bounded, maximum);
    cvar.AddChangeHook(SettingChanged);
    return cvar;
}

public void OnPluginStart() {
    if (GetEngineVersion() != Engine_Left4Dead2) SetFailState("L4D2 only");
    cvEnable = Setting("enable", "1", "Master switch", true);
    cvHp = Setting("boss_hp_enable", "1", "Boss health HUD", true);
    cvMsg = Setting("map_msg_enable", "1", "Map message HUD", true);
    cvRank = Setting("boss_rank_enable", "1", "Contribution collection and settlement", true);
    cvRankHud = Setting("boss_rank_hud_enable", "1", "Ranking HUD", true);
    cvRankChat = Setting("boss_rank_chat_enable", "1", "Ranking chat", true);
    cvBossSlot = Setting("hud_boss_slot", "0", "One HUD slot (0-14)");
    cvMsgSlots = Setting("hud_msg_slots", "1,2", "Exactly two distinct HUD slots");
    cvRankSlots = Setting("hud_rank_slots", "3,4,5,6", "Title and three ranking slots");
    cvInterval = Setting("hud_interval", "0.10", "HUD refresh seconds", true, 0.05, 1.0);
    cvHudFrame = Setting("hud_frame_force", "1", "0 write on change only; 1 rewrite owned HUD slots from the ~0.1 s timer; 2 rewrite them every game frame", true, 0.0, 2.0);
    cvHudBackend = Setting("hud_backend", "1", "0 write GameRules netprops directly; 1 VScript HUDSetLayout via smHud.inc", true);
    g_HudBackend = cvHudBackend.IntValue;
    cvHpHold = Setting("boss_hp_hold", "3.0", "Health hold seconds", true, 0.1, 60.0);
    cvMsgHold = Setting("map_msg_hold", "11.0", "Message hold seconds (countdown messages use their own seconds)", true, 0.1, 60.0);
    cvFilter = Setting("map_msg_filter_mode", "0", "0 strict rules; 1 loose server chat", true);
    cvHideChat = Setting("map_msg_hide_chat", "0", "1 hides map messages shown on HUD from chat", true);
    cvCountdown = Setting("map_msg_countdown", "1", "1 adds a live countdown to 'N seconds/minutes' map messages", true);
    cvTranslate = Setting("map_msg_translate", "1", "1 shows map messages translated by maps/<map>/translations.cfg", true);
    cvPageTime = Setting("rank_page_time", "5.0", "Seconds per ranking page", true, 1.0, 30.0);
    cvRankHudTime = Setting("rank_hud_time", "60.0", "Seconds the ranking stays on HUD", true, 5.0, 300.0);
    cvChatTime = Setting("rank_chat_interval", "0.20", "Seconds per chat line", true, 0.1, 2.0);
    cvBots = Setting("rank_include_bots", "1", "Include bots from next fight", true);
    cvDebug = Setting("debug", "0", "0 off; 1 state; 2 detailed diagnostics", true, 0.0, 2.0);
    cvNoticeHud = Setting("credit_notice_hud_enable", "1", "Show credited scripted damage (materia) in the ranking HUD region", true);
    cvNoticeChat = Setting("credit_notice_chat_enable", "1", "Print credited scripted damage (materia) to chat", true);
    cvNoticeHold = Setting("credit_notice_hold", "6.0", "Seconds a scripted damage notice stays on HUD", true, 1.0, 30.0);
    CreateConVar("l4d2_moreinfo_version", VERSION, "Plugin version", FCVAR_NOTIFY|FCVAR_DONTRECORD);
    g_Messages = new ArrayList(sizeof(MapMessage));
    g_Results = new ArrayList(sizeof(Result));
    g_OutputHooks = new StringMap();
    g_Pending = new ArrayList(sizeof(PendingCredit));
    g_Notices = new ArrayList(sizeof(CreditNotice));
    g_CreditHooks = new StringMap();
    for (int b = 0; b < MAX_BOSSES; b++) {
        g_Boss[b].contributions = new ArrayList(sizeof(Contribution));
        g_Boss[b].materia = new ArrayList(sizeof(MateriaRow));
        g_Boss[b].healthRef = INVALID_ENT_REFERENCE;
    }
    for (int e = 0; e < MAX_ENTS; e++) g_EntityBoss[e] = -1;
    RegAdminCmd("sm_moreinfo_status", CommandStatus, ADMFLAG_CONFIG);
    RegAdminCmd("sm_moreinfo_reload", CommandReload, ADMFLAG_CONFIG);
    RegAdminCmd("sm_moreinfo_probe", CommandProbe, ADMFLAG_CONFIG);
    RegAdminCmd("sm_moreinfo_dump", CommandDump, ADMFLAG_CONFIG);
    RegAdminCmd("sm_moreinfo_hudtest", CommandHudTest, ADMFLAG_CONFIG);
    HookEvent("round_start", RoundStart, EventHookMode_PostNoCopy);
    HookEvent("round_end", RoundEnd, EventHookMode_PostNoCopy);
    HookEvent("mission_lost", RoundEnd, EventHookMode_PostNoCopy);
    HookMessages();
    AddCommandListener(ObserveSay, "say");
    AutoExecConfig(true, "l4d2_moreinfo");
    CreateTimer(0.05, Tick, _, TIMER_REPEAT);
}

public void OnMapStart() {
    g_MapReady = true;
    g_RoundLive = true;
    g_Epoch++;
    g_ScanQueued = false;
    for (int e = 0; e < MAX_ENTS; e++) { g_Hooked[e] = false; g_EntityBoss[e] = -1; }
    HudInitialize();
    ResetRuntime(g_Late);
    char error[256];
    if (!LoadConfiguration(false, error, sizeof(error))) LogError("Config: %s", error);
    RequestFrame(ScanFrame, g_Epoch);
}

public void OnConfigsExecuted() {
    int slots[7];
    if (ReadSlots(slots)) for (int i = 0; i < 7; i++) g_Slots[i] = slots[i];
    if (g_MapReady && g_Late && cvEnable.BoolValue) ScanBindings();
}

public void OnMapEnd() {
    ReleaseHud();
    g_MapReady = false;
    g_RoundLive = false;
    g_Epoch++;
    ResetRuntime(false);
    g_Late = false;
}

public void OnPluginEnd() {
    ReleaseHud();
    ClearResults();
}

public void RoundStart(Event event, const char[] name, bool dontBroadcast) {
    g_RoundLive = true;
    g_Late = false;
    g_Epoch++;
    ResetRuntime(false);
    RequestFrame(ScanFrame, g_Epoch);
}

public void RoundEnd(Event event, const char[] name, bool dontBroadcast) {
    g_RoundLive = false;
    g_Epoch++;
    ResetRuntime(false);
    ReleaseHud();
}

public void OnGameFrame() {
    // A different plugin may supercede damage and prevent the matching post hook.
    // Contexts cannot be carried into the next simulation frame.
    // 其他插件可能拦截伤害，导致对应的 post 钩子不触发；伤害上下文不能带到下一帧。
    if (g_Depth > 0) {
        for (int i = 0; i < g_Depth; i++) {
            int b = g_Context[i].boss;
            if (b >= 0 && b < g_BossCount) g_Boss[b].partial = true;
        }
        g_Depth = 0; g_Overflow = 0;
    }
    HudFrame(2);
}

void ResetRuntime(bool partial) {
    g_Depth = 0;
    g_Overflow = 0;
    g_Messages.Clear();
    ClearResults();
    for (int b = 0; b < MAX_BOSSES; b++) ResetBoss(b, partial);
    ResetCredits();
    for (int e = 0; e < MAX_ENTS; e++) g_EntityBoss[e] = -1;
    g_NextHud = 0.0;
}

public void SettingChanged(ConVar cvar, const char[] oldValue, const char[] newValue) {
    if (g_Reverting || g_Messages == null) return;
    if (cvar == cvBossSlot || cvar == cvMsgSlots || cvar == cvRankSlots) {
        int slots[7];
        if (!ReadSlots(slots)) {
            LogError("Rejected invalid/duplicate/reserved HUD slots: %s", newValue);
            g_Reverting = true;
            cvar.SetString(oldValue);
            g_Reverting = false;
            return;
        }
        ReleaseHud();
        for (int i = 0; i < 7; i++) g_Slots[i] = slots[i];
    }
    if (cvar == cvHudBackend) {
        // Clear with the old backend before switching. / 先用旧后端清空再切换。
        ReleaseHud();
        g_HudBackend = cvHudBackend.IntValue;
    }
    if (cvar == cvEnable) {
        g_Epoch++;
        ResetRuntime(true);
        ReleaseHud();
        if (cvEnable.BoolValue && g_MapReady) RequestFrame(ScanFrame, g_Epoch);
    }
    if (cvar == cvRank) {
        ClearResults();
        for (int b = 0; b < g_BossCount; b++) {
            g_Boss[b].contributions.Clear();
            g_Boss[b].materia.Clear();
            g_Boss[b].unknown = 0.0;
            g_Boss[b].partial = true;
        }
        ResetCredits();
    }
    if (cvar == cvMsg && !cvMsg.BoolValue) g_Messages.Clear();
    if (cvar == cvNoticeHud && !cvNoticeHud.BoolValue) g_Notices.Clear();
    if ((cvar == cvRankHud && !cvRankHud.BoolValue) || (cvar == cvRankChat && !cvRankChat.BoolValue)) {
        Result r;
        for (int i = 0; i < g_Results.Length; i++) {
            g_Results.GetArray(i, r);
            if (cvar == cvRankHud) r.hudDone = true;
            else r.chatDone = true;
            g_Results.SetArray(i, r);
        }
    }
    g_NextHud = 0.0;
}

public Action Tick(Handle timer) {
    if (!g_MapReady || !g_RoundLive || !cvEnable.BoolValue) return Plugin_Continue;
    PollBosses();
    FlushCredits(-1);
    PumpRankings();
    float now = GetGameTime();
    if (now >= g_NextHud) {
        g_NextHud = now + cvInterval.FloatValue;
        RenderHud();
    }
    HudFrame(1);
    return Plugin_Continue;
}
