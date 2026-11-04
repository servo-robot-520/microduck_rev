# microduck 声音系统：合唱与节拍

> 对象：`sounds/src/chorale/` 与 `robotd/src/chorale.rs`、`btd/src/chorale.rs`。
> 主题：四只鸭子如何在没有共享时钟的情况下唱同一首四声部曲子。
> 架构见 [原型架构](microduck-sounds-architecture.zh-CN.md)；合成见 [合成原理](microduck-sounds-synthesis.zh-CN.md)。

---

## 0. 一句话结论

合唱把“声音身份”和“音高”拆开了：

- **音高是绝对的**：A4 = 440 Hz，十二平均律，所有鸭子读同一份 `Score`。
- **身份是音色**：每只鸭子保留自己的谐波权重、formant、鼻音、breath，以及被压小的 vibrato。
- **同步靠 beat，不靠时钟**：conductor 在 BLE beacon 里放一个每拍递增的计数器；follower 把“计数器变化”当作 downbeat，用滑动窗口平均相位。
- **不追求毫秒级齐奏**：刻意加入 ±5 cents 的失谐和 ±15 ms 的起音偏移，否则四只鸭子会像一个风琴音栓。

---

## 1. 数据模型

### 1.1 `Part`

```rust
enum Part { Bass, Tenor, Alto, Soprano }   // 低 → 高
```

| Part | `register()`（MIDI 附近） | 典型音域（默认曲目） |
|---|---|---|
| Bass | 51 | A2..A3 |
| Tenor | 58 | E3..E4 |
| Alto | 63 | A3..A4 |
| Soprano | 68 | D4..D5 |

`Part as usize` 直接索引 `Voicing = [Option<u8>; 4]`，所以枚举顺序是协议的一部分，不能重排。

`Part::ensemble(n)` 决定 n 只鸭子唱哪些声部：

| n | 声部 | 理由 |
|---|---|---|
| 1 | `[Soprano]` | 独唱取旋律 |
| 2 | `[Bass, Soprano]` | 和弦最需要外声部；Bass+Tenor 会变成同八度的嘟囔 |
| 3 | `[Bass, Alto, Soprano]` | 去掉 Tenor，它的音最常被其他声部重复 |
| ≥4 | `[Bass, Tenor, Alto, Soprano]` | 完整 SATB |

这些集合是**嵌套**的：`{S} ⊂ {B,S} ⊂ {B,A,S} ⊂ {B,T,A,S}`。嵌套是“新鸭子加入时，已经在唱的人不用换声部”的前提；`seat` 依赖它，保证第四个鸭子到来前总有一个空声部。

### 1.2 `Note`

```rust
struct Note {
    part: Part,
    start_beat: f64,
    beats: f64,
    midi: u8,
    level: f64,      // 0..1，写入的动态
    vowel: Vowel,
}
```

`Note` 是**所有前端收敛后的唯一形态**：手写文本和 MIDI 都编译成它。理由很实际：MIDI 里没有 `Gesture` 可恢复，只有音符时间；下游（渲染、编排、同步）只应该读一种东西。

### 1.3 `Vowel`

| 元音 | `open()`（嘴） | `formant_shift()`（谐波） | `level_scale()` |
|---|---|---|---|
| `Ah` | 0.90 | 0 | 1.0 |
| `Eh` | 0.65 | +1 | 1.0 |
| `Ee` | 0.45 | +3 | 1.0 |
| `Oh` | 0.55 | −1 | 1.0 |
| `Oo` | 0.35 | −2 | 1.0 |
| `Mm` | 0.05 | −2 | 0.65 |

不是语音学，是舞台元音：`oo` 的 0.15 张嘴在机器人上看起来像闭着嘴唱歌，所以统一夸张化。只有 `Mm` 保持闭嘴——哼唱本来就该闭嘴。`open()` 同时是嘴部舵机的目标值，所以元音是**听得见也看得见**的同一个数。

### 1.4 `Gesture` 与 `Score`

`Gesture` 是“写谱的人脑子里的动作”，`Score` 是编译后的音符列表：

| Gesture | 语义 | 编译结果 |
|---|---|---|
| `Chord { voicing, beats, vowel, level }` | 所有人同时唱一个和弦 | 每个非 `None` 声部一个 `Note` |
| `Build { voicing, beats, stagger, from_top, vowel, level }` | 声部依次进入，一起收尾 | 第 k 个声部 `start = at + k*stagger`，`beats = beats - k*stagger`；负长度直接丢弃 |
| `Solo { part, notes, under, under_vowel, level }` | 一个声部走动，其他声部持和弦 | 伴奏每个非 `None` 声部一个长音；独唱按 `SoloNote` 顺序排 |
| `Rest { beats }` | 所有人休息 | 不产生音符，但占用时间，从而打断 tie |

`Score`：

```rust
struct Score {
    name: String,
    bpm: f64,
    notes: Vec<Note>,   // 按 part、再按 start 排序
}
```

`Score::from_gestures` 先展开成 `Note`，再调用 `from_notes`；`from_notes` 负责排序和 tie 合并。两条前端（文本、MIDI）都走这里。

### 1.5 `Singer`

```rust
struct Singer {
    personality: Personality,
    part: Part,
    detune_cents: f64,      // ±5
    onset_offset_s: f64,    // ±0.015
}
```

`detune_cents` 和 `onset_offset_s` 由 seed 派生，不是全局随机：同一组鸭子永远是同一组“轻微不齐”。这是“合唱”与“一个风琴音栓”的差别。

---

## 2. 乐谱前端

### 2.1 文本 `.duckscore`

一行一个 gesture，`#` 到行尾是注释。全局指令：

```text
name: <text>
bpm: <number>          # 20..300
dynamic <mark>         # ppp pp p mp mf f ff，运行状态
vowel <name>           # ah eh ee oh oo mm，运行状态
```

`dynamic` 和 `vowel` 是**运行状态**：写一次，对之后开始的音符生效，直到再次改变。

Gesture：

```text
chord <beats> B T A S
build <beats> stagger <b> [top] B T A S
solo <part> under B T A S [hum <vowel>] sing <note>[:beats][/vowel] ...
rest <beats>
```

- 声部永远按低到高写：Bass、Tenor、Alto、Soprano；`-` 表示该声部不唱。
- 音名用科学音高：C4 = MIDI 60（记谱软件约定），`F#4`/`Gb4` 都可，大小写不敏感。
- `solo` 的 `under` 默认让伴奏哼 `mm`；`hum <vowel>` 可改。独唱音符默认使用当前 `vowel`，`/ee` 可逐音覆盖，`:2` 指定拍数（默认 1）。
- 解析错误带行号、原文和原因；错误信息本身要能定位到“哪一行、哪个词”。

`Score::wistful()` 用 `include_str!("../../scores/wistful.duckscore")` 嵌入默认曲目，所以“发布的曲子”和“格式示例”是同一个文件，不会漂移。`wistful` 是原创的，避免版权问题。

### 2.2 MIDI 前端

`chorale::midi::parse` 直接解析 Standard MIDI File，不引入第三方 crate。支持范围：

- `MThd`/`MTrk`、running status、note-on/note-off（velocity 0 视为 note-off）；
- tempo meta（第一个 tempo 生效；后续变化会报告“已丢弃”，不会静默唱错速度）；
- track name；
- 结束时仍按着的音符在 track 末尾收尾；
- 未知 chunk 按长度跳过；
- 错误：不是 MIDI、截断、SMPTE 时基、没有音符。

**声部归属按音高，不按 track 顺序**：

1. 每个 `(track, channel)` 是一个候选声部组；
2. 如果只有一个组（一个钢琴谱表导出，和弦都在一起），按每个 onset 内的音高排名拆成 4 个声部；
3. 按每组的平均音高排序，最低的组唱 Bass；
4. 如果 track 名字包含 `soprano`/`alto`/`tenor`/`bass`（或 `sop`），优先相信名字；
5. 超过 4 个声部时保留最低的 4 个，并报告丢弃了几个；
6. velocity 变成 `level = clamp(velocity/100, 0.15, 1.0)`。

为什么不是 track 顺序：记谱软件通常从上往下写（Soprano 在前），正好和 `Voicing` 的低到高相反；DAW 还可能带 tempo track、空 track、乱序 track。按音高排序无论文件怎么产生都正确，而且和 `cast` 的规则一致。

`duck_strut` 用 `scores/duck_strut.mid` 嵌入，是“用记谱软件编辑、提交导出文件”的工作流示例：D 大调、126 bpm、oom-pah bass、tenor 背拍、中段 tenor 低八度回声。`outer_wilds` 是测试专用、受版权保护，发布前必须移除。

### 2.3 tie 合并：合唱与“一串和弦”的差别

`Score::from_notes` 按 `part`、再按 `start_beat` 排序，然后合并：

```text
if 同一 part
   && 同一 midi
   && 同一 vowel
   && |next.start - prev.end| < 1e-6:
       prev.beats += next.beats
```

关键细节：

- **pitch 相同、vowel 相同、时间连续**才合并。共同音因此是一个长音，而不是每个和弦重新起音。
- **vowel 改变**会重新起音：歌手换音节就会重新发声。
- **dynamic 改变不合并**：一个 `f` 标记落在已经唱着的音上，是渐强，不是重新起音；tie 保留音符开始时的动态。
- **`Rest` 打断 tie**：中间有休止，前后同音必须分开，否则“呼吸”会被合并吞掉。

这套语义由 `common_tones_come_out_as_one_note`、`a_rest_is_a_real_gap`、`a_change_of_syllable_or_dynamic_is_a_new_note` 等测试钉住。

---

## 3. 声部编排（casting）

### 3.1 `cast`：整组一起开始

```text
parts = Part::ensemble(n)
order = 按 pitch_center_hz 升序，同音高按 seed 升序
for rank, index in order:
    part    = parts[min(rank, parts.len()-1)]
    rng     = personality.variant_rng("chorale-seat", rank)
    detune  = uniform(-5, +5) cents
    onset   = uniform(-15, +15) ms
```

- 最低的鸭子唱 Bass，最高的唱 Soprano。
- 完全由 seed 决定：两只鸭子不需要协商谁唱什么，只要交换 seed 就能得到同一结果。
- 输入顺序不影响结果（测试 `casting_does_not_depend_on_the_order_the_ducks_arrive_in`）。
- 超过 4 只时多出的鸭子重复最高声部；真实无线电路径最多 4 只（`MAX_ROSTER=4`），这个分支只有离线渲染能到。

### 3.2 `seat`：中途加入

`seat(existing, newcomer)` 的核心承诺是：**已经在唱的人不换声部**。

```text
wanted = Part::ensemble(held.len() + 1)
free   = wanted - held
choices = free.is_empty() ? wanted : free
part   = choices 中 register 最接近 newcomer 的声部
```

- 因为 `ensemble` 的集合嵌套，第四个鸭子到来之前总有一个空声部；新鸭子进空隙，不用挪人。
- 如果新鸭子的音域比当前 Bass 还低，它仍然会拿到空声部（例如 Alto），而不是把 Bass 挤走；这是“不挪人”优先于“绝对按音高”的取舍。
- 第 5 只开始重复声部，选最接近自己音域的那个。
- `seat` 的 rng 用 `existing.len()` 作为 seat 号，所以同一只鸭子在同一个加入位置上永远得到同一组 detune/onset。

### 3.3 `seat_all`：无线电路径的唯一编排

`seat_all(registers)` 是 conductor 广播 roster 后，每只鸭子独立重放的那一个函数：

```text
parts = Part::ensemble(n)
order = 按 register 升序，同 register 按 roster 位置
for rank, position in order:
    seated[position] = parts[rank]   // 超出部分重复 Soprano
```

- **按音高分配，不按到达顺序**。早期版本按 join order 折叠 `seat_by_register`，结果 `ensemble(1)=[Soprano]` 把第一个到达的鸭子（哪怕它是低音）塞进 Soprano，第二个塞进 Bass——两只真鸭子实测时低音那只在唱高音。
- 返回数组的顺序与 roster 一致；声部按音高分配。
- 因为 `ensemble` 嵌套，新鸭子若落在缺失的音域，其他人不动；若落在当前 Bass 之下，会移动一个声部——这是“一开始就让所有人唱对线”的代价。

### 3.4 `seat_for`：已知声部时的 detune/onset

无线电路径里，鸭子从 conductor 的 roster 知道自己的 `Part`，但 detune/onset 仍要从自己的 seed 派生：

```text
rng = personality.variant_rng("chorale-seat", part as u32)
detune = ±5 cents; onset = ±15 ms
```

这样同一只鸭子唱同一声部时，永远带来同一组微小失谐/偏移。

### 3.5 为什么没有自动移调

代码里有一段明确的设计说明：尝试过“把整首曲子移到每只鸭子自己的中心附近”，结果每个合奏都被推到最大移调、低音变薄。原因是 `pitch_center_hz` 是**叫声**的中心，不是歌唱音域；谐波权重是相对 f0 的，鸭子唱低一个八度仍然像自己。音域选择已经由 `cast`/`seat_all` 完成，再叠一个移调只会和 voicing 打架。`Options::transpose` 保留为手动口味旋钮。

---

## 4. 渲染

### 4.1 离线混音 `render`

```text
total = (score.duration_s() + 2.5) * SR
for each singer:
    sing(score, singer, transpose, total, options)  → 加到 mix
gain = 1 / sqrt(n)                                  # 不是 1/n
if room > 0: reverb(&mut mix, room)
normalise(&mut mix, peak_dbfs)                      # 默认 -3 dBFS
```

- 每只鸭子**整段渲染后相加**，不是逐块交错。真实硬件上它们就是四台独立机器，任何需要共享缓冲的做法都只是在预览一个无法实现的东西。
- `1/sqrt(n)`：不相关信号的能量相加；`1/n` 会让每加一只鸭子整体更轻，而不是更饱满。
- 尾部多留 2.5 s 给最后一个 release 和混响衰减。
- `Options` 默认：`transpose=0`、`room=0.25`、`peak_dbfs=-3`、`speaker_rolloff_hz=Some(300)`。

### 4.2 单只鸭子 `sing`

```text
BLOCK = 128 样本
stream = Stream::choral(personality, part as u32)
stream.set_speaker_rolloff(options.speaker_rolloff_hz)
detune = 2^(detune_cents / 1200)
now    = sample_index/SR - onset_offset_s
```

对每个控制块：

1. 推进 `note_index`，直到 `now < note.end_beat()*beat_s`。
2. 如果当前音符已开始：
   - `last_hz = midi_hz(note.midi + transpose) * detune`；
   - `sustain = note.beats*beat_s*0.92`：提前 8% 释放，换和弦时重新起音而不是滑过去；
   - `level = note.level * vowel.level_scale()`（`Mm` 是 0.65）；
   - `open = vowel.open() + 0.12*reach`，`reach` 是该音符在**本声部自己的音域**内的归一化位置（唱得越高，嘴张得越大）；
   - `formant = vowel.formant_shift()`。
3. 否则：`level=0`，`open` 用默认 `Ah` 的 0.90，`formant=0`。`last_hz` 保持不变：休止时声音在“最后一个音高上”淡出，而不是滑向别处。
4. `set_formant_shift`、`set(last_hz, level, open)`、`block(&mut out[start..end])`。

`last_hz` 跨休止保留是刻意的：下一个音从上一个音高附近进入，比从“无处”滑上来自然。

### 4.3 混响

`reverb` 是 Schroeder 结构：

| 部分 | 参数 |
|---|---|
| 4 个并联 comb | 29.7 / 37.1 / 41.1 / 43.7 ms，反馈 0.78 / 0.76 / 0.74 / 0.72，各乘 0.25 后相加 |
| 2 个串联 allpass | 5.0 / 1.7 ms，增益 0.7 |
| 混合 | `dry*(1-0.5*wet) + wet_sum*wet` |

延迟按 `SR` 缩放，保持房间的**尺寸**而不是样本数。默认 `room=0.25`：真实鸭子是房间里的四个物体，干声预览会比硬件更刺；但混响不是核心，`room=0` 仍然能渲染。

### 4.4 实时渲染

`robotd` 的 `sing_start` 与离线 `sing` 是同一套逻辑的另一种组织：

- 不遍历整段样本，而是每个 tick 读一次 `sing_at(beats, singing)` 发布的 score 位置；
- 写线程按 `at = beats*beat_s - seat.onset_offset_s` 找当前 `Note`；
- `Stream::choral`、`set_speaker_rolloff(300)`、`set_formant_shift`、`set`、`block(10 ms)`；
- `CHORALE_LEVEL=0.55`（见架构文档 §5.5）。

区别只在“谁决定 `beats`”和“块多大”，音色与音符选择规则一致。

---

## 5. 无共享时钟的节拍同步

### 5.1 目标不是“同一毫秒”

四个人同时起音、完全同频，听起来不是合唱，而是一个风琴音栓。合唱感来自**几乎在一起、几乎同音**：

- `detune_cents`：±5 cents；
- `onset_offset_s`：±15 ms。

所以同步的目标是 **±20 ms**，不是 ±1 ms。和弦的**音准**必须精确，但音准只需要共享 A4=440，不需要任何时钟。

### 5.2 conductor 是一个 beacon

没有可共享的时钟：板子没有 RTC 协议、没有 NTP，建立时钟偏移需要连接/配对/central 角色，而 `btd` 没有这些。

替代方案：

```text
conductor:
  每拍把 beat 计数器 +1，放进 BLE manufacturer data
  自己的播放比计数器晚 SLOT_MEAN_S = 0.125 s
follower:
  被动扫描；计数器变化 = 新的一拍
  记录“这一拍到达本机的本地时间”
  用滑动窗口平均相位
```

不需要时间戳比较、不需要估计 offset、不需要连接、不需要配对。被动扫描是蓝牙控制器最便宜的操作。

### 5.3 为什么槽位抖动不会毁掉它

空中时间是微秒级，误差主要来自**广播槽位**：conductor 把新 payload 交给控制器，控制器在下一个槽位发出去；`btd` 的槽位被刻意拉开（一个天线要同时承载 BLE、手柄和 WiFi）。follower 听到每一拍的时间落在一个约 50 ms 的窗口里，而且**每拍独立**。

两个机制把它变成 ±几毫秒：

1. **conductor 延迟自己的播放** `SLOT_MEAN_S=0.125 s`，让自己和“大家听到的”在同一个方向上错，而不是默认早。
2. **follower 不追单拍**：对滑动窗口内的相位取平均。独立抖动随窗口长度按 `1/sqrt(W)` 下降。

`WINDOW=24`：50 ms 均匀槽位的单拍标准差约 14.4 ms；24 拍平均后约 2.9 ms；两个独立 follower 之间约 4 ms，远小于 20 ms 预算。24 拍在合唱速度下约 25 秒，但 follower 在第 4 拍就已经可以开口。

### 5.4 不估计 tempo，这是修正而不是偷懒

第一版用 `(beat, arrival)` 做直线拟合，从斜率读 tempo——教科书做法，实测更差：

- 直线在窗口**最新点**（也就是“现在到哪了”）的误差约为窗口中心处的 4 倍，因为斜率不确定性会随离质心的距离放大；
- 两个模拟 follower 因此差出 36 ms。

现在 tempo 直接取乐谱的 `bpm`，只平均相位。这样去掉了斜率项，误差降到约三分之一。物理上也更诚实：两只鸭子读同一份谱，晶振差是 ppm 级（一分钟约 3 ms），远小于估计器自己贡献的几十毫秒。

### 5.5 `Conductor`

```text
beat_s = 60 / max(bpm, 1)
due(now):
    should = floor(max(now-start, 0) / beat_s)
    if emitted == Some(last) && should <= last: None
    else: emitted = Some(should); Some(should)
```

- `emitted` 是 `Option<u64>`，不是从 0 开始的计数器：“还没发过”和“第 0 拍已发”是不同状态。早期用 0 初始化会让 `due` 每次 poll 都重新宣布第 0 拍（广播率 8 次/秒），follower 的拟合会建立在噪声上。
- `position_beats(now) = (now - start - SLOT_MEAN_S) / beat_s`：conductor 唱的是房间**听到**的位置，不是它刚交给电台的位置。
- `wire_beat()` 返回 `emitted % 256`：一个字节在合唱速度下约 4 分钟，follower 负责解包。

### 5.6 `Follower`

```text
observe(beat_u8, at):
    unwrapped = unwrap(beat_u8)
    if phase exists && heard.len() >= 4:
        predicted = phase + beat_s * unwrapped
        if |at - predicted| > 0.20: return       # 离群值丢弃
    heard.push({beat: unwrapped, at})
    if len > 24: remove oldest
    phase = mean(at_i - beat_s * beat_i)
```

- **只在计数器变化时调用**。一个 beacon 每拍会重复多次（`beat.rs` 的模型里约七八次，实际次数取决于广播间隔）；重复读同一个值不是新拍，会把相位拖晚半个广播间隔。
- `unwrap` 处理 `u8` 回绕：以 `last/256` 为基，候选值落在 `last±128` 之外就加减 256。计数器只前进，所以“看起来倒退”就是回绕。
- 离群值阈值 0.20 s：一次排队的扫描或陈旧的 advertisement 不能污染平均；半秒的离群值会把 24 拍窗口拖得比 16 个好拍拉回来的还多。
- `locked()` 要求至少 4 拍：单拍相位带着整个槽位窗口的误差，第一拍就开口会明显早或晚。4 拍约一小节，也是歌手加入一段已经在进行的歌所需的时间。
- `position_beats(now) = (now - phase) / beat_s`；`spread_s` 是各拍隐含相位的标准差，用于诊断“锁得紧不紧”。

### 5.7 电台消失时继续唱

follower 听不到任何东西时：

- 保持最后的 `phase`，按 score tempo 继续走；
- 精度由晶振决定，每分钟几毫秒；
- conductor 回来时，新拍被正常 `observe`，不会跳变。

这正是歌手听不到指挥时的做法，也是为什么修正是“缓慢拖 tempo”而不是“跳一下”。

测试 `silence_from_the_conductor_is_not_silence_from_the_duck` 用 10 秒静默验证：位置继续前进，conductor 回来时偏移 <0.1 拍。

### 5.8 beacon 线格式

`ChoraleBeacon`：

| 字段 | 宽度 | 含义 |
|---|---|---|
| `piece` | u8 | 正在唱的曲目 id；0 = 愿意加入但还没开始 |
| `beat` | u8 | conductor 的拍计数，回绕 |
| `register` | u8 | 自己的 `pitch_center_hz` 量化到 100–625 Hz，约 2 Hz/步 |
| `id` | u16 | seed 派生的 16 位身份/去重/自我反射过滤 |
| `roster` | 变长 | `(register, id)` 列表，按 seating 顺序；最多 4 个 |

- `TAG=0xC0` 区分“合唱 beacon”和同一 company id 下的其他四字节（例如另一个广播实例里的 IPv4 地址）。
- `MAX_ROSTER=4`：一首曲子四个声部；第五只鸭子继续听，不加入。
- `id` 从 8 位扩到 16 位是因为真实四鸭房间第一天就撞了 8 位 id：第四只鸭子的 id 与 conductor 相同，被所有人合并成一只，且把自己的反射当成自己而拒绝，永远无法加入。
- `register` 的量化范围包住 `Personality` 的 110–620 Hz，两端留余量。

`btd` 侧：

- 合唱 beacon 使用**第二个广播实例**，不改动原有的可发现广播；按需注册，合唱结束即释放。
- 广播间隔 20–40 ms（`BEACON_INTERVAL_MIN/MAX`），比普通广播的 100–150 ms 快，因为 payload 变化到达空中的及时性就是同步误差。
- 监听优先用 BlueZ advertisement monitor（控制器内过滤、被动），板子上没有该接口时退回普通 discovery；退回路径需要 `duplicate_data`，并在主机侧按 tag 过滤。
- 到达时间在 `btd` 进程看到 advertisement 时打戳，包含控制器/内核/D-Bus 的几毫秒延迟；设计上依赖“随机抖动会被平均，固定偏移对所有同硬件鸭子是共模”。

### 5.9 行为层状态机

`robotd/src/chorale.rs`：

| 状态 | 行为 |
|---|---|
| `Off` | 不广播 |
| `Listening` | 广播 idle beacon；听到已知曲目就加入；听到别人但自己不是最小 id 就等；没有 peer 不独唱 |
| `Conducting` | 维护 roster（只增不减、保持顺序）、`Conductor`、选曲、广播 piece/beat/roster、发布自己的 `(part, position)` |
| `Following` | 锁定第一个唱歌的 conductor（按 beacon `id`，绝不按蓝牙地址）、跟随 beat、广播 idle beacon、从 roster 重放 `seat_all` 得到自己的声部 |

关键规则：

- **conductor 选举**：有 peer 且自己的 `id` 最小，并经过 `SETTLE=1500 ms`，才 conduct。两个能互相看见的鸭子不会都以为只剩自己。
- **roster 只增不减**：离开的鸭子保留座位，否则下面的人会换声部；`voices` 只统计仍然听得到的，所以“少一个声部”而不是“所有人挪位”。
- **conductor 拥有编排**：follower 不自己 `seat`，而是重放 `seat_all(roster)`。这是“两只鸭子都唱 Alto”的根因修复。
- **曲目注册表**：conductor 广播 piece id；follower 不认识就继续听，不猜。混版本鸭群中旧鸭子保持安静。
- **重启检测**：piece 变化，或 beat 计数器向后跳 4..127 拍，视为 conductor 重启；重置 follower 和乐谱。小幅向前跳只是漏拍。
- **演出结束**：conductor 超过曲尾 +2 拍回到 `Listening`；follower 超过 +6 拍才自行结束，让 conductor 的重启先导。

### 5.10 位置发布

控制回路每 tick 从 `Chorale::tick()` 得到 `Tick.singing = Some((part, beats))`，然后：

```text
sound.sing_start(chorale.score(), part)   # 幂等
sound.sing_at(beats, singing)
```

写线程只读最新 `beats`，所以音频卡顿不会累积成“落后一小节”：位置是时间的函数，不是已播放样本的计数。

---

## 6. 实现与模型的偏差（移植前必须处理）

`beat.rs` 的模型假设广播槽位是 **100–150 ms**，因此 `SLOT_MEAN_S = 0.125`；它的测试也用 100–150 ms 的均匀分布。但 `btd/src/chorale.rs` 当前把合唱 beacon 的广播间隔设为 **20–40 ms**，并明确说这比普通广播的 100–150 ms 快。

这两者不能同时正确：

- 如果真实到达抖动是 20–40 ms，`SLOT_MEAN_S=0.125` 会让 conductor 把自己的播放延迟约 95 ms，超出 ±20 ms 预算，且是**共模之外的系统性偏移**（conductor 相对所有 follower 错）。
- 如果真实槽位仍是 100–150 ms（例如控制器把 20–40 ms 的请求合并到更慢的调度），则当前常量是对的，但 `btd` 的注释和常量会误导。

移植时不要照抄 0.125。应在目标硬件上测量“交给电台 → follower 看到”的分布，然后：

1. 用实测均值设置 conductor 的延迟；
2. 用实测标准差设置 `Follower::WINDOW`（窗口需要 `σ/√W` 落在预算内）；
3. 让测试使用同一组实测参数，而不是继续用 100–150 ms 的理想化槽位。

---

## 7. 不变量与测试

| 性质 | 验证 |
|---|---|
| 最低的鸭子唱 Bass，顺序无关，纯 seed 决定 | `the_lowest_duck_sings_bass`、`casting_does_not_depend_on_the_order_the_ducks_arrive_in` |
| 新鸭子加入不移动已在唱的人 | `a_duck_joining_does_not_move_anyone_already_singing` |
| roster 按音高分配，不按到达顺序 | `the_lowest_voice_in_the_roster_gets_the_lowest_part` |
| ensemble 集合嵌套，每次只开一个空位 | `the_ensemble_sets_are_nested` |
| 第五只鸭子重复最接近自己音域的声部 | `a_fifth_duck_doubles_the_part_nearest_its_voice` |
| 同一 MIDI 音符在所有鸭子那里是同一频率 | `every_duck_sings_the_same_pitch_for_the_same_note` |
| 共同音 tie 成一个长音，换元音/休止才重新起音 | `common_tones_come_out_as_one_note`、`a_rest_is_a_real_gap` |
| 默认曲目是四声部、在鸭子音域内、有动态/元音/分进入 | `the_default_score_stays_inside_ranges_a_duck_can_sing`、`the_default_piece_is_more_than_block_chords` |
| 任意 2–4 只鸭子都渲染出有限、可听、不削波的音频 | `an_ensemble_of_any_size_renders_sane_audio` |
| 四重奏在 50 ms 槽位抖动下最坏两两偏差 <20 ms | `a_whole_quartet_agrees_within_the_ensemble_budget` |
| 窗口平均确实降低抖动 | `the_fit_averages_the_jitter_down` |
| u8 回绕不导致音乐跳回 | `the_beat_counter_wraps_without_the_music_jumping` |
| conductor 静默时继续按 tempo 唱，回来能接上 | `silence_from_the_conductor_is_not_silence_from_the_duck` |
| 离群值被丢弃而不是平均进去 | `an_outlier_is_dropped_rather_than_averaged_in` |
| 不到 4 拍不开口 | `a_follower_waits_before_it_sings` |
| conductor 只每拍递增一次，不随 poll 递增 | `the_conductor_counts_beats_and_not_polls` |
