# L4d2-MoreInfo

求生之路 2（L4D2）SourceMod 插件，给自定义地图的 Boss 战和地图提示加上 HUD 显示：

- **Boss 血量**：屏幕上方显示当前 Boss 的名字、阶段和剩余血量。
- **地图消息**：把地图用聊天框发出的提示（开门、坚守、Boss 出现等）搬到屏幕上方，可以翻译成中文，带秒数的提示会实时倒计时。
- **贡献排名**：Boss 被击败后，按每位玩家实际打掉的血量排名，显示在 HUD 右侧并发到聊天框。

版本 1.0.2。插件需要为每张地图单独配置，自带配置的地图只有 [FFVII Mako Reactor（魔晄炉）](#自带地图魔晄炉)。

## 显示效果

```
               机械 Boss 阶段1: 8420 / 12000              ← Boss 血量
          [32秒] 大门将在 45 秒后开启                     ← 地图消息（最新两条）
          Boss 前来阻止你们
                                        机械 Boss 1/2 未归属:120   ← 排名标题（页码）
                                        1. PlayerA  5230.0
                                        2. PlayerB  3110.5
                                        3. PlayerC  1508.0
```

聊天框里的结算：

```
[MoreInfo] 机械 Boss 已击败 · 有效伤害
[MoreInfo] 1. PlayerA  5230.0
[MoreInfo] 2. PlayerB  3110.5
...
[MoreInfo] 未归属: 120.0
```

- 排名每页 3 人，自动翻页，默认在 HUD 上停留 60 秒。
- “未归属”是插件无法确定是谁造成的扣血，比如地图机关、延迟触发的伤害。插件不会猜测归属。
- 中途加载插件或配置有问题时，排名会标记为 `[不完整]`。
- 中途离开的玩家贡献会保留。
- HUD 对所有生还者共享，聊天排名只发给真人生还者。

## 安装

依赖：Left 4 Dead 2 服务器、Metamod:Source、SourceMod 1.12 或更新版本（自带 SDKTools、SDKHooks）。

仓库只提供源码，需要自己编译出 `l4d2_moreinfo.smx`：

1. 把仓库里的 `addons` 和 `cfg` 两个文件夹复制到服务器的 `left4dead2` 目录，合并目录结构。
2. 用 SourceMod 自带的编译器编译。在 `addons/sourcemod/scripting` 目录执行 `spcomp l4d2_moreinfo.sp -o../plugins/l4d2_moreinfo.smx`（Windows 用 `spcomp.exe`）。`l4d2_moreinfo/` 子文件夹要和 `.sp` 放在一起。
3. 换图，或在服务器控制台执行 `sm plugins load l4d2_moreinfo`。
4. 执行 `sm_moreinfo_status` 确认插件已运行。

安装后的文件：

| 文件 | 作用 |
| --- | --- |
| `addons/sourcemod/scripting/l4d2_moreinfo.sp`、`scripting/l4d2_moreinfo/` | 插件源码 |
| `addons/sourcemod/plugins/l4d2_moreinfo.smx` | 编译出的插件本体 |
| `cfg/sourcemod/l4d2_moreinfo.cfg` | 开关和参数（cvar），带中英文说明 |
| `addons/sourcemod/configs/l4d2_moreinfo/hud.cfg` | HUD 各区域的位置和大小 |
| `addons/sourcemod/configs/l4d2_moreinfo/maps/<地图名>/config.cfg` | 该地图的 Boss 和消息规则 |
| `addons/sourcemod/configs/l4d2_moreinfo/maps/<地图名>/translations.cfg` | 可选，该地图消息的中文翻译 |
| `addons/sourcemod/configs/l4d2_moreinfo/maps/example/` | 空白模板，不会被加载 |

没有配置的地图上，插件不显示 Boss 血量，也不会搬运地图消息，不影响正常游戏。

## 配置项（cvar）

写在 `cfg/sourcemod/l4d2_moreinfo.cfg` 里，换图时读取。也可以在控制台临时修改，但不会自动保存。

**总开关**

| Cvar | 默认 | 说明 |
| --- | --- | --- |
| `l4d2_moreinfo_enable` | 1 | 插件总开关。关闭时清空正在进行的统计 |
| `l4d2_moreinfo_debug` | 0 | 日志：0 关闭；1 状态；2 详细诊断（适配地图时用） |

**Boss 血量**

| Cvar | 默认 | 说明 |
| --- | --- | --- |
| `l4d2_moreinfo_boss_hp_enable` | 1 | 显示 Boss 血量。关闭不影响排名统计 |
| `l4d2_moreinfo_boss_hp_hold` | 3.0 | 最后一次打中 Boss 后，血量条继续显示的秒数 |

**贡献排名**

| Cvar | 默认 | 说明 |
| --- | --- | --- |
| `l4d2_moreinfo_boss_rank_enable` | 1 | 统计贡献并结算排名 |
| `l4d2_moreinfo_boss_rank_hud_enable` | 1 | 在 HUD 显示排名 |
| `l4d2_moreinfo_boss_rank_chat_enable` | 1 | 在聊天框发送排名 |
| `l4d2_moreinfo_rank_hud_time` | 60.0 | 排名在 HUD 上停留的秒数（5～300） |
| `l4d2_moreinfo_rank_page_time` | 5.0 | 每页停留秒数（1～30） |
| `l4d2_moreinfo_rank_chat_interval` | 0.20 | 聊天排名每行的间隔秒数 |
| `l4d2_moreinfo_rank_include_bots` | 1 | 排名包含电脑队友。设为 0 时电脑的伤害算作未归属，从下一场 Boss 战生效 |
| `l4d2_moreinfo_credit_notice_hud_enable` | 1 | 神器伤害命中时在排名区域实时提示（Boss 战斗中该区域空闲） |
| `l4d2_moreinfo_credit_notice_chat_enable` | 1 | 神器伤害命中时在聊天框提示 |
| `l4d2_moreinfo_credit_notice_hold` | 6.0 | 实时提示停留秒数（1～30） |

**地图消息**

| Cvar | 默认 | 说明 |
| --- | --- | --- |
| `l4d2_moreinfo_map_msg_enable` | 1 | 把地图消息显示到 HUD。聊天框里的原消息保留 |
| `l4d2_moreinfo_map_msg_filter_mode` | 0 | 0 严格：只显示地图配置里规则匹配的消息；1 宽松：显示所有服务器聊天消息 |
| `l4d2_moreinfo_map_msg_hold` | 11.0 | 普通消息的显示秒数。倒计时消息显示到归零后 2 秒 |
| `l4d2_moreinfo_map_msg_hide_chat` | 0 | 1 = 已显示到 HUD 的消息不再出现在聊天框 |
| `l4d2_moreinfo_map_msg_countdown` | 1 | 1 = 含“N seconds / minutes”的消息前面加 `[N秒]` 实时倒计时 |
| `l4d2_moreinfo_map_msg_translate` | 1 | 1 = 按 `translations.cfg` 把消息翻译后显示，没收录的句子显示原文。只影响 HUD |

**HUD 槽位**

| Cvar | 默认 | 说明 |
| --- | --- | --- |
| `l4d2_moreinfo_hud_interval` | 0.10 | HUD 刷新间隔秒数（0.05～1） |
| `l4d2_moreinfo_hud_frame_force` | 1 | 重写本插件的 HUD 槽位，防止被导演系统、地图脚本或其他插件重置：0 只在内容变化时写入（1.0.1 的逻辑）；1 由约 0.1 秒一次的循环计时器重写；2 每帧重写（HUD 仍闪烁时使用） |
| `l4d2_moreinfo_hud_boss_slot` | 0 | Boss 血量用的槽位 |
| `l4d2_moreinfo_hud_msg_slots` | 1,2 | 地图消息用的两个槽位 |
| `l4d2_moreinfo_hud_rank_slots` | 3,4,5,6 | 排名用的 4 个槽位：标题 + 3 行 |

L4D2 的 HUD 一共有 15 个槽位（0～14）。如果和其他插件抢同一个槽位，把本插件的槽位改到空闲编号即可。

## 管理命令

需要 SourceMod 的配置权限（`ADMFLAG_CONFIG`，即 `i`），服务器控制台可以直接执行。

| 命令 | 作用 |
| --- | --- |
| `sm_moreinfo_status` | 查看各开关、已加载的 Boss / 消息规则 / 翻译条数、HUD 槽位和 Boss 状态 |
| `sm_moreinfo_reload` | 重新读取地图配置、翻译和 `hud.cfg`。配置有错时拒绝重载并保留原配置 |
| `sm_moreinfo_hudtest [秒数]` | 在 7 个 HUD 区域显示测试文字，默认 15 秒，0 立即结束。用来检查位置和遮挡 |
| `sm_moreinfo_probe [实体编号]` | 查看准星指向的实体（或指定编号）的类名、名称、Hammer ID 和血量 |
| `sm_moreinfo_dump` | 把当前地图所有实体信息导出到 `addons/sourcemod/logs/l4d2_moreinfo_entities.log` |

## 调整 HUD 位置

编辑 `addons/sourcemod/configs/l4d2_moreinfo/hud.cfg`，然后执行 `sm_moreinfo_reload`。

```
"boss"       { "x" "0.20" "y" "0.14" "width" "0.60" "height" "0.04" }
```

- 坐标和宽高都是屏幕比例（0～1），左上角是 0,0。
- 共 7 个区域：`boss`、`message_1`、`message_2`、`rank_title`、`rank_1`～`rank_3`。区域之间不能重叠。
- `reserved_slots` 填其他插件或地图占用的槽位（逗号分隔），本插件不会使用它们。
- 如果发现某个槽位被别人改写，插件会停止写入这个槽位。解决冲突后执行 `sm_moreinfo_reload`。

默认布局已经避开魔晄炉魔晶石插件（l4d2_mako_entwatch）右下方的显示区域。

## 适配新地图

每张地图一个文件夹，文件夹名必须和地图名完全一致（控制台 `status` 里显示的名字）：

1. 把 `maps/example/` 复制为 `maps/<地图名>/`。
2. 进入这张地图，用 `sm_moreinfo_probe` 瞄准 Boss 的受击体，或用 `sm_moreinfo_dump` 导出全部实体，找到存血量的实体、开始信号和结束信号。
3. 填写 `config.cfg` 的 `bosses` 和 `messages`，需要翻译的话再填写 `translations.cfg`。
4. 执行 `sm_moreinfo_reload`，再用 `sm_moreinfo_status` 确认 `Boss:`、`消息规则:`、`翻译:` 的数量对得上。

模板里每个配置项都写了中英文说明，完整的成品写法参考 `maps/l4d2_ffvii_makoreactor/`。配置文件是 UTF-8 编码的 KeyValues 格式。

### Boss 配置

`bosses` 下每个小节是一个 Boss，小节名是唯一 id：

| 字段 | 说明 |
| --- | --- |
| `name` | HUD 和排名里显示的名字 |
| `unit` | 排名数值的单位，默认“有效伤害”。按命中次数扣血的地图建议写“地图血量点” |
| `cancel` | 可选。这个信号触发时，本场作废、不结算 |
| `phases` | 阶段列表 `"1"`、`"2"`……按顺序进行，最后一个阶段结束时才结算，前面阶段的贡献会累计 |

每个阶段的字段：

| 字段 | 说明 |
| --- | --- |
| `model` | `health`：血量是实体的 health；`counter_down`：`math_counter` 往下数到 `counter_min`；`counter_up`：往上数到 `counter_max` |
| `health` | 存血量的实体。计数器模式必须是 `math_counter` |
| `hitboxes` | 玩家实际打的实体，可以写多个。`health` 模式不写时，受击体就是 `health` 实体 |
| `max_hp` | 固定的最大血量。地图按人数设置血量时不写，改用 `capture_max_on_start` |
| `capture_max_on_start` | 1 = `start` 触发时把当前血量记为最大血量 |
| `start_delay` | `start` 触发后等多少秒再读血量（0～60），用于地图延迟设置血量的情况 |
| `counter_min` / `counter_max` | 计数器的下界 / 上界 |
| `attribution` | `verified_sync`：伤害记到攻击者名下；`unknown`（默认）：只显示血量，伤害全部算未归属 |
| `start` | 可选，阶段开始信号。不写时血量实体一出现就开始，排名会标记为不完整 |
| `finish` | 必填，阶段结束信号，最后一个阶段必须是真正的击杀信号，比如 `OnBreak`、`OnHitMin` |

- 实体选择器写 `classname`，再加 `targetname` 或 `hammer_id`，也可以两个都写。如果同一个选择器能匹配到多个实体，插件会拒绝绑定，这时要加上 `hammer_id` 区分。
- 信号就是选择器加上 `output`，例如 `OnTrigger`、`OnBreak`、`OnEntitySpawned`。
- `start` 要选在地图设置好血量之后触发的输出，否则最大血量会读错。地图在输出之后再延迟加血的，用 `start_delay` 等它加完。
- 只有确认过“玩家打一下，实体血量当场就减少”时，才用 `verified_sync`。如果地图脚本会在伤害时重置血量或回血，请保留 `unknown`。
- 数量上限：每张地图 16 个 Boss，每个 Boss 4 个阶段，每个阶段 8 个受击体，每场 256 名贡献者。
- 实体被直接删除不算击杀，本场作废。

### 脚本伤害归属（credited，可选）

有些地图用脚本直接扣 Boss 血（比如魔晄炉的神器），没有攻击者，默认计入未归属。如果能确认“谁按下按钮”和“脚本扣多少血”，可以在 Boss 下写 `credited`：

```
"credited"
{
    "ultima"
    {
        "label" "终极"
        "caster" { "classname" "func_button_timed" "targetname" "M_Ultima_Button" "output" "OnTimeUp" }
        "apply"
        {
            "1" { "classname" "func_button_timed" "targetname" "M_Ultima_Button" "output" "OnUser1" "amount" "25000" "delay" "0" }
        }
    }
}
```

- `caster`：玩家触发的信号，触发者（activator）必须是生还者，否则这次伤害仍算未归属。
- `apply`：地图真正扣血的输出，每项最多 4 个。`amount` 是这次扣的血量，`delay` 是输出到实际扣血之间的动作延迟（秒）。`apply` 必须在 `caster` 之后 3 秒内触发。
- 只有 `delay` 之后约 1 秒内观察到的掉血、且不超过 `amount` 的部分会记给施放者，多出来的仍算未归属。脚本用 `Break` 打死 Boss 时，剩余血量记给施放者。
- 这部分伤害会并入玩家总伤害，结算时另外列出“神器伤害”页。没有 `credited` 的地图行为和 1.0.1 完全一样。
- 每张地图最多 16 条。

### 消息规则

`messages` 下每条规则可以写 `prefix`、`contains`、`exclude`、`key`，匹配时不区分大小写：

- `exclude`：消息里出现任意一条规则的 `exclude` 文本，这条消息就不显示。它优先于所有其他规则。
- `prefix` / `contains`：从上往下，第一条“以 prefix 开头，并且包含 contains”的规则生效。所以具体的规则要放在宽泛的规则前面。
- `key`：`key` 相同的新消息会立刻顶替旧消息，比如倒计时和随后的“正在开启”。
- 每张地图最多 32 条规则。

只处理服务器发出的消息，玩家聊天不会上 HUD。HUD 显示最新两条，文字太长时分段轮换。

### 翻译

`translations.cfg` 只改变 HUD 上显示的文字。规则匹配和倒计时仍然按原文进行。

```
"Translations"
{
    "doors_in" { "en" "Doors will open in {n} seconds" "zh" "大门将在 {n} 秒后开启" }
}
```

- `en` 写原句，去掉两边的 `**` / `***`。按整句匹配，不区分大小写，多个空格当作一个。
- `{n}` 匹配一个整数，并原样填进 `zh`。每条最多一个 `{n}`，一张地图最多 96 条。
- 没收录的句子按原文显示。设置 `l4d2_moreinfo_debug 1` 后，这些句子会以 `Untranslated map message: ...` 写进日志，方便补表。
- 翻译文件格式有错时，全部显示原文，不影响 Boss 和消息功能。

### 排查

- 设置 `l4d2_moreinfo_debug 2` 会记录实体输出、伤害回调和 HUD 写入，SourceMod 日志里可以看到每一步有没有触发。
- `sm_moreinfo_status` 会列出每个 Boss 的状态（等待开始 / 进行中 / 结束中 / 已结算 / 已取消）、阶段、血量，以及是否标记为“统计不完整”或“选择器冲突”。
- 修改配置后执行 `sm_moreinfo_reload`。如果只改了名字或 HUD 布局，正在进行的统计会保留。改了血量实体、信号等机制时，正在进行的战斗会作废。

## 自带地图：魔晄炉

`maps/l4d2_ffvii_makoreactor/` 适配的是 FFVII Mako Reactor（`l4d2_ffvii_makoreactor`，BSP mapversion 7002），其他版本的地图可能对不上。

| Boss | 出现条件 |
| --- | --- |
| 机械 Boss | 简单 / 普通难度 |
| 巴哈姆特 | 困难 / 极限难度 |
| 萨菲罗斯（桥） | 桥上堵路时 |

- 血量由地图脚本按人数和难度设置，插件在 Boss 出现时读取，不需要手动填写。
- 武器伤害按实际扣血计入攻击者。巴哈姆特的神器（终极、冰、火、雷）伤害记给按下神器按钮的玩家，并入总伤害；地图超时、其他脚本改血量计入未归属。
- 神器命中时 HUD 排名区域和聊天框会实时提示，例如 `张三 使用[终极] 对 巴哈姆特 造成 25000 伤害`。结算时 HUD 先翻完总伤害页，再翻神器伤害页（只列出有神器伤害的玩家），然后从第一页循环；聊天框在未归属之后列出神器伤害。
- 地图的所有提示都已收录并翻译成中文，译名采用最终幻想VII 官方简体中文译名：魔晄炉、巴哈姆特、萨菲罗斯、魔晶石。
- 开门、坚守和倒计时共用一行，电梯共用一行，Boss 提示共用一行，新消息会顶替同类旧消息。管理员切换难度的提示不会上 HUD。
- 加载正常时，`sm_moreinfo_status` 显示 `Boss:3 消息规则:13 翻译:59 神器归属:4`。
- 请从新的一回合开始打，中途加载插件的那场排名会标记为不完整。

## 常见问题

**Boss 血量不显示**
先确认当前地图有配置：`sm_moreinfo_status` 里显示 `Boss:0`（当前地图未配置 Boss）说明没有加载到。检查文件夹名是否和地图名一致，并查看 SourceMod 错误日志。

**地图消息没有上 HUD**
严格模式下只显示规则匹配的消息，没有配置 `messages` 的地图不会显示任何消息。想临时看所有服务器消息，可以设置 `l4d2_moreinfo_map_msg_filter_mode 1`。如果装了 consolechatfilter 这类屏蔽控制台聊天的插件，它可能先把消息拦掉。想隐藏聊天框里的地图消息，请改用 `l4d2_moreinfo_map_msg_hide_chat 1`。

**本地开房（非专用服务器）**
地图的 `say` 消息会显示成房主发言。此时只有匹配地图规则的消息会上 HUD，宽松模式不起作用。

**排名显示“无可确认归属的玩家贡献”或未归属很多**
这张地图的伤害无法可靠地对应到玩家，比如配置里用的是 `"attribution" "unknown"`，或者伤害由地图机关造成。插件只统计能确认的伤害，不会猜。

**HUD 和其他插件重叠**
用 `sm_moreinfo_hudtest` 看清各区域位置，然后调整 `hud.cfg` 的坐标，或者改用其他槽位。

## 限制

- 只支持按地图手动配置的 Boss，不会自动识别 Tank、Witch 等官方特感。
- 不读取地图 VScript 内部的变量，血量必须能从实体或计数器上读到。
- 投射物等无法确认使用者的伤害计入未归属。脚本伤害只有配置了 `credited` 才会归属。

## 许可

[GPL-3.0](LICENSE)
