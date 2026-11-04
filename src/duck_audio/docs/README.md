# duck_audio 文档

本目录记录 **microduck 原型的声音实现**，作为 `duck_audio` 移植与验收的参考。

原型的代码基线是 `pollen-robotics/microduck` 工作区中的：

- `sounds/`：可移植的合成 crate（seed、Personality、7 个 tag、实时流、合唱）。
- `robotd/src/sound.rs`：运行时的唯一 PCM 所有者（一次性音效、`wheee` 三段流、特雷门、合唱声部）。
- `robotd/src/chorale.rs` + `btd/src/chorale.rs`：合唱行为与 BLE beacon 传输。
- `robotd/src/intents.rs`、`duck-ipc-proto/`：`robot.sound` / `robot.theremin` / `robot.chorale` 的意图入口。

ROS 2 下的目标架构（`audio_io`、`duck_audio`、`duck_ear` 的分层与接口）在仓库顶层文档
[`docs/duck-ros2-audio-architecture.zh-CN.md`](../../../docs/duck-ros2-audio-architecture.zh-CN.md)，
原型文档只描述**原型实际做了什么、为什么这样做**；实现文档描述 `microduck_rev` 中 `duck_audio` 如何复用原型并接入 Orbbec Gemini。

## 实现文档（`duck-ros2-*`）

| 文档 | 内容 |
|---|---|
| [duck_audio ROS 2 架构](duck-ros2-architecture.zh-CN.md) | C++ 节点、Rust FFI、线程/仲裁、ROS 接口、部署与验收 |
| [Rust FFI 边界](duck-ros2-rust-ffi.zh-CN.md) | C ABI、所有权、panic 隔离、Cargo/CMake 构建、ABI 测试 |
| [Gemini 特雷门](duck-ros2-theremin-gemini.zh-CN.md) | 用 Orbbec 点云/深度图替代原 ToF：坐标、ROI、手部跟踪、映射与标定 |

## 原型文档

| 文档 | 内容 |
|---|---|
| [原型架构](microduck-sounds-architecture.zh-CN.md) | 模块边界、seed/身份、音库、运行时播放、线程与失败模式 |
| [合成原理](microduck-sounds-synthesis.zh-CN.md) | DSP 基元、Personality 派生、7 个音色配方、实时 `Stream`、小喇叭补偿 |
| [合唱与节拍](microduck-sounds-chorale.zh-CN.md) | 乐谱模型、文本/MIDI 前端、声部编排、无共享时钟的 beat 同步 |

## 五条必须保留的性质

1. **音库是确定性的**：同一 seed 永远得到同一 `Personality`；同一 `(tag, variant)` 永远是同一段音频。
2. **变体不是随机音色**：`variant` 只在同一 seed 内重掷细节，音色身份由 seed 决定。
3. **播放设备只有一个所有者**：同一时刻只有一条 `aplay` 持有 PCM；抢占、跳过、淡出都是显式状态。
4. **实时流与音库是同一只鸭子**：特雷门/合唱用 `Stream`，谐波权重、formant、breath 来自同一个 `Personality`，不是另一套合成器。
5. **声音失败不拖垮运动**：没有音库、没有 seed、没有声卡时，`robotd` 照常行走，只是安静。

## 术语

| 术语 | 含义 |
|---|---|
| seed | `u32`，SoC serial 经 sha256 得到；一切声音身份的根 |
| `Personality` | 由 seed 派生的 21 个稳定音色/节奏参数 |
| tag | 语义音效名：`alarm/greet/inquire/peck/chirp/coo/wheee` |
| variant | 同一 tag 内的小幅重掷，音库中以字母 `a..l` 命名 |
| bank | `/var/lib/robot/sounds` 下预渲染的 WAV 集合 |
| `Stream` | 逐块合成、参数可实时改变的同一套合成器 |
| `Score` / `Part` / `Note` | 合唱的乐谱、四个声部、编译后的音符 |
| beat | 合唱的共享时间基；由 conductor 的 BLE 计数器传播 |

## 约定

- 代码符号用反引号，路径相对于原型仓库根。
- “必须”描述原型已经依赖的性质；“注意/风险”描述实现与模型不一致、移植时不能照抄的地方。
- 本文档不把原型的 Rust 常量当作 ROS 2 接口的一部分；接口以顶层架构文档为准。
