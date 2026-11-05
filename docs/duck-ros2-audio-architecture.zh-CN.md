# Microduck 声音系统 ROS 2 架构

> 对象：`microduck_rev`（ROS 2 Humble，C++23，`ament_cmake`）。
> 依据：`pollen-robotics/microduck` 的 `sounds`、`robotd/src/sound.rs`、`theremin`、`chorale`、`pet-detect`，以及当前开发板的实测结果。
> 状态：`audio_io`、`audio_io_interface`、`duck_audio`（voice/wheee/特雷门）、`duck_audio_interface`、`duck_ear`、`tts_interface`、`tts`（piper/paroli）已实现；合唱暂不实现。`duck_ear` 运行时需要 ONNX Runtime，`paroli` 后端需要 paroli-server。语音识别（ASR/唤醒词）为独立层，见 [`duck-ros2-asr-architecture.zh-CN.md`](./duck-ros2-asr-architecture.zh-CN.md)。
> `audio_io` 详细架构：[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)
> 约定：节点名用 `snake_case`；话题/服务/动作名相对于节点私有命名空间书写，例如 `~/play`。

---

## 0. 一句话结论

声音系统拆成三层：

1. **通用音频 I/O 层**：`audio_io` 功能包（库 + `audio_io_node`）独占播放和采集设备，负责 WAV/MP3 解码、混音、优先级、抢占和 PCM 流。
2. **通用 TTS 层**：`tts_node` 负责文字 → 音频，提供 service/topic/action，不碰声卡。
3. **microduck 语义层**：`duck_audio` 一个节点承载 `voice`、`wheee`、`theremin`；合唱暂不实现；`duck_ear` 独立负责摸头检测。

三条硬约束：

- **播放设备只有一个所有者**：`audio_io`。其他节点只发 `AudioClip` 或 `AudioStream`。
- **麦克风只有一个所有者**：`audio_io` 的采集侧。`duck_ear`、未来的 WebRTC 都订阅 `~/capture`。
- **通用接口不出现鸭子语义**：`chirp`、`coo`、`wheee`、seed、personality 只属于 `duck_audio_interface`；`audio_io` 只使用 `audio_io_interface`。

---

## 1. 范围与非目标

### 1.1 范围

- 播放预渲染音库（`alarm/greet/inquire/peck/chirp/coo/wheee`）。
- 播放音频文件（当前实现：WAV/MP3），支持循环、增益、起始偏移；FLAC/OGG 为后续扩展。
- 播放实时 PCM 流（特雷门）；流式 TTS 尚未实现。
- TTS：文字 → 音频；service 只合成，topic/action 合成并播放。
- 麦克风采集、摸头检测、环境声事件。
- 语音识别：订阅同一路采集，做唤醒词、固定口令与后续自由识别；架构见
  [`duck-ros2-asr-architecture.zh-CN.md`](./duck-ros2-asr-architecture.zh-CN.md)。
- 优先级、抢占、排队、同组替换、混音/ducking。
- 设备热插拔、错误降级、状态上报。

### 1.2 非目标

- **`audio_io` / `duck_audio` 不做语音识别（ASR）、唤醒词、对话理解**。ASR 是独立层 `asr`，
  鸭子语义在 `duck_asr`；本文只约束它们共用采集设备。`pet-detect` 只判断“是否在摸头”，环境声
  `Voice` 只是 RMS 包络事件。
- **不做云 TTS 强依赖**。当前只有离线 `piper`/`paroli` 后端；eSpeak 与云后端未实现。
- **不在本节点内做 WebRTC 传输**。`mediad` 未来需要麦克风/扬声器时，通过 `AudioStream` 接入 `audio_io`。
- **不把声音合成塞进 `ros2_control` 的 update loop**。声音不是 50 Hz 运动控制的一部分；它有自己的线程和生命周期。

---

## 2. 原型行为摘要（移植时必须保留的性质）

| 原型位置 | 行为 | ROS 2 中的对应 |
|---|---|---|
| `sounds/src/lib.rs` | SoC serial → sha256 → `u32` seed → `Personality` | `duck_audio` 启动时解析 seed；音库 `.seed` 标记优先 |
| `sounds/src/voices.rs` | 7 个 tag；`greet`/`chirp` 12 变体，`wheee` 6 变体（start/loop/end 三段），其余 10 变体 | 预渲染 WAV 音库，运行时不合成一次性叫声 |
| `sounds/src/synth.rs` | 48 kHz、16-bit mono、加法谐波合成 | 音库格式；实时流也用 48 kHz |
| `robotd/src/sound.rs` | 单个 `aplay` 子进程，新声音杀旧声音 | `audio_io` 的单播放引擎 + 显式抢占策略 |
| `robotd/src/sound.rs` | `wheee` start→loop→end 流式写入 | `AudioStream` + `eos`/超时 |
| `robotd/src/sound.rs` | 特雷门用 `Stream` 实时合成，30 ms 提前量 | `duck_audio` 的 theremin 模块发 `AudioStream`，`audio_io` 只负责播 |
| `robotd/src/sound.rs` | 合唱按 score beat 渲染自己声部 | 当前未实现；接口保留在 `duck_audio_interface` |
| `pet-detect` | `arecord` 16 kHz mono → 40-band log-mel → 20 KB CNN | `audio_io` 采集 → `duck_ear` 订阅并推理 |
| `robotd` IPC | `robot.sound` / `robot.theremin` / `robot.chorale` | 专用 ROS 2 service/topic/action |

必须保留的性质：

1. **音库是确定性的**：同一个 seed 永远得到同一只鸭子的声音；变体只在同一 seed 内重掷。
2. **播放是单设备的**：同一时刻只有一个引擎拥有 PCM；抢占/排队是显式策略，不是“碰巧谁先打开”。
3. **小喇叭补偿**：低频谐波搬到扬声器能发出的频段；默认 300 Hz rolloff，可按硬件配置。
4. **麦克风单客户端**：采集设备不能被 `duck_ear` 和 `mediad` 各自打开。
5. **声音失败不拖垮运动**：没有音库、没有麦克风、没有声卡，机器人仍然能走。

---

## 3. 分层与节点命名

```mermaid
flowchart TB
  subgraph G["通用层（可复用于其他机器人）"]
    AIO["/audio_io<br/>audio_io_node<br/>播放 + 采集 + 混音 + 优先级"]
    TTS["/audio_tts<br/>tts_node<br/>piper / paroli + 合成调度"]
  end

  PS["paroli-server<br/>外部进程，可选"]

  subgraph P["duck_asr_node 进程（默认组合 libasr）"]
    SR["asr::AsrNode<br/>/asr<br/>KWS + VAD + ASR"]
    DS["duck_asr<br/>/duck_asr<br/>口令 → 意图 / 回复"]
    SR -->|"AsrEvent"| DS
  end

  ASRN["asr_node<br/>独立测试 / 其他机器人"]

  subgraph D["microduck 其他节点"]
    DA["/duck_audio<br/>voice / wheee / theremin"]
    DE["/duck_ear<br/>log-mel + ONNX"]
    BEH["行为/控制节点<br/>duck_control 等"]
  end

  APP["App / agent / 遥控"] -->|"TtsText"| TTS
  APP -->|"AudioClip"| AIO
  TTS -->|"AudioClip"| AIO
  AIO -->|"AudioEvent"| TTS
  TTS -.->|"HTTP / paroli 后端"| PS
  DA -->|"AudioClip / AudioStream"| AIO
  AIO -->|"AudioStream / ~/capture"| DE
  AIO -->|"AudioStream / ~/capture"| SR
  AIO -->|"AudioEvent（播放门控）"| SR
  AIO -.->|"~/capture"| ASRN
  ASRN -.->|"AsrEvent（asr.compose=false）"| DS
  SR -.->|"AsrEvent（后期 agent）"| APP
  DS -->|"DuckIntent"| BEH
  DS -->|"TtsText"| TTS
  DS -.->|"PlayVoice（可选）"| DA
  DE -.->|"~/petting/event（未订阅）"| DA
  DE -->|"~/petting/event"| BEH
  BEH -->|"动作目标"| CTRL["duck_control / controller_manager"]
  AIO -->|"ALSA / null"| SPK["speaker"]
  MIC["USB / onboard mic"] --> AIO
```

### 3.1 包与节点

| 层 | 包 | 可执行文件 | 节点全名 | 复用性 |
|---|---|---|---|---|
| 通用音频 I/O | `audio_io` | `audio_io_node` | `/audio_io`（可选 namespace） | 是 |
| 通用音频消息 | `audio_io_interface` | — | — | 是 |
| 通用 TTS（已实现） | `tts` | `tts_node` | `/audio_tts`（可选 namespace） | 是 |
| 通用 TTS 消息（已实现） | `tts_interface` | — | — | 是 |
| 通用语音识别（设计） | `asr` | `asr_node` | `/asr`（可选 namespace） | 是 |
| 通用语音识别消息（设计） | `asr_interface` | — | — | 是 |
| 鸭子口令语义（设计） | `duck_asr` | `duck_asr_node` | `/duck_asr`（可选 namespace） | 否 |
| 鸭子口令消息（设计） | `duck_asr_interface` | — | — | 否 |
| 鸭子音频（已实现 voice/wheee/特雷门） | `duck_audio` | `duck_audio_node` | `/duck_audio`（可选 namespace） | 否 |
| 鸭子耳朵（已实现） | `duck_ear` | `duck_ear_node` | `/duck_ear`（可选 namespace） | 否 |
| 鸭子音频消息（已实现） | `duck_audio_interface` | — | — | 否 |

命名规则：

- 通用音频包用 `audio_io`、`audio_io_interface`；TTS 用 `tts`、`tts_interface`；机器人专用包用 `duck_*`（`duck_audio`、`duck_ear`）。
- 可执行文件统一 `*_node`；节点名是角色名词，不是实现细节。
- 不要用 `voice_node`、`sound_node`、`audio_node` 这类没有职责信息的名字。
- 不要用 `sound_play`：ROS 生态已有同名包，且它不解决本文的优先级/会话/混音需求。
- 多机器人时用命名空间 `/robot1/audio_io`、`/robot1/audio_tts`、`/robot1/duck_audio`、`/robot1/duck_ear`，节点名不变；`tts` 的 `audio_io.node` 要同步指向对应命名空间。

### 3.2 `duck_audio` 的边界

`voice`、`wheee`、`theremin` 共享音库、seed、`Personality` 和同一个 Rust `Stream`，因此合并成一个
`/duck_audio` 节点；内部仍按模块拆分。组合式节点、线程/仲裁、Gemini 输入、接口和 FFI 细节见：

**[`src/duck_audio/docs/duck-ros2-architecture.zh-CN.md`](../src/duck_audio/docs/duck-ros2-architecture.zh-CN.md)**

### 3.3 当前接线状态

| 链路 | 状态 |
|---|---|
| App/agent → `/audio_tts` | 可用：`~/text`、`~/synthesize`、`~/say`；内部单工作线程调度 |
| `/audio_tts` → `/audio_io` | 可用：`AudioClip`（WAV）+ `AudioEvent` 回读；不发布 `AudioStream` |
| `/audio_tts` → paroli-server | 可选：`backend=paroli` 时 HTTP 合成；未启动则 TTS 请求失败 |
| App → `/audio_io` | 可用：直接发 `AudioClip` 播放文件 |
| `/duck_audio` → `/audio_io` | 可用：一次性音效 `AudioClip`，wheee/特雷门 `AudioStream` |
| `/audio_io` → `/duck_ear` | 可用：`~/capture`，`stream_id=mic` |
| `/duck_ear` → `/duck_audio` | **未接线**：`~/petting/event` 已发布，`duck_audio` 未订阅，不会自动 `coo` |
| `/duck_ear` → 行为/控制 | 事件可用，具体动作/超时/互斥由行为或控制节点实现 |
| `/audio_io` → `/asr` | 设计：`~/capture`，`stream_id=mic`；播放门控订阅 `~/event` |
| `/asr` → `/duck_asr` | 设计：同进程组合（`asr.compose=true`），`AsrEvent` 的 `EVENT_WAKE`/`EVENT_FINAL` |
| `/duck_asr` → `/audio_tts`、`battery_monitor` | 设计：电量回复；姿势/动作只发 `DuckIntent`，执行待 `duck_control` 命令接口 |

### 3.4 语音识别层

唤醒词、VAD、ASR、口令语义不属于 `audio_io`，也不属于 `duck_audio`/`duck_ear`：

- 通用层 `asr`：订阅 `~/capture`，做重采样、VAD、唤醒词、ASR；输出纯文本事件。
- 鸭子层 `duck_asr`：组合 `libasr`，把固定口令映射成电量查询、姿势/动作意图与 TTS 回复；不实现 ASR 后端。
- 固定口令必须编译成解码约束，不是事后字符串匹配；词表由 `duck_asr` 唯一维护。
- 播放期间必须做半双工门控，否则会识别到自己的 TTS/叫声。

完整设计、接口、参数、失败模式与验收见：

**[`docs/duck-ros2-asr-architecture.zh-CN.md`](./duck-ros2-asr-architecture.zh-CN.md)**

---

## 4. 通用音频 I/O：`audio_io`

`audio_io` 的职责、内部结构、播放/采集、接口、QoS、声道语义、参数、包布局、构建与验收已迁移到独立仓库文档：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

系统级只保留三条约束：

- `audio_io` 是播放和采集设备的唯一所有者；
- 其他节点只通过 `AudioClip` / `AudioStream` 提交数据；
- `audio_io` 不感知鸭子语义、TTS、ToF 或摸头检测。

---

## 5. `duck_audio`：microduck 语义层

`duck_audio` 的职责、组合式节点、voice/wheee/特雷门、Gemini 深度输入、接口、参数、FFI 边界、
构建与验收已迁移到独立文档：

**[`src/duck_audio/docs/duck-ros2-architecture.zh-CN.md`](../src/duck_audio/docs/duck-ros2-architecture.zh-CN.md)**

配套文档：

- [Rust FFI 边界](../src/duck_audio/docs/duck-ros2-rust-ffi.zh-CN.md)
- [Gemini 特雷门](../src/duck_audio/docs/duck-ros2-theremin-gemini.zh-CN.md)

系统级只保留这些约束：

- `duck_audio` 是鸭子语义层，不碰 ALSA；一次性声音发 `AudioClip`，实时声音发 `AudioStream`。
- 音库、seed 和 `Personality` 由 `duck_audio` 持有；默认音库在 `~/.config/duck_ros2/sounds`，启动时自动检查/生成。
- 一次性音效与 `wheee` 走音库；特雷门用 Rust `Stream` 实时合成，Gemini 深度图/点云只提供 `closeness`。
- 合唱暂不实现；`duck_audio_interface` 中的 `SetChorale`/`ChoraleState` 保留给后续。
- 声音失败只影响声音，不影响运动控制。

---

## 6. 麦克风与 `duck_ear`

`duck_ear` 的职责、输入契约、重采样/log-mel/CNN、事件语义、参数、依赖、失败模式和测试已迁移到独立文档：

**[`src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md`](../src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md)**

系统级只保留这些约束：

- `audio_io` 是麦克风设备的唯一所有者；`duck_ear`、未来的 WebRTC 都订阅 `~/capture`，不各自打开 ALSA。
- `duck_ear` 只做感知，发布 `~/petting/event`（`duck_audio_interface/PettingEvent`），不播放声音、不写舵机。
- 重采样、VAD、mel、CNN 不进入 `audio_io`，也不进入 `duck_audio`。
- `duck_ear` 运行时需要 ONNX Runtime 和模型；缺失时节点保持运行，只是不发布摸头事件。
- `duck_audio` 当前没有订阅 `petting`，因此不会自动 `coo`；动作由行为/控制节点消费事件后执行。
- 采集健康看 `audio_io` 的 `~/state`（`capture_ok`/`capture_error`）和 `~/event`。

原版 `pet-detect` 在摸头开始时只触发 `coo`，没有额外运动动作；“摸头后做特定动作”是新增行为，需要单独定义动作语义、超时和安全互斥。


---

## 7. TTS：`/audio_tts`

`tts` 功能包的职责、接口、后端、缓存、参数、延迟预算和运行方式已迁移到独立仓库文档：

**[`src/tts/docs/tts-ros2-architecture.zh-CN.md`](../src/tts/docs/tts-ros2-architecture.zh-CN.md)**

系统级只保留这些约束：

- `tts` 是通用 TTS 层，不碰声卡；输出以 `AudioClip` 交给 `audio_io`，自己不打开设备。
- 接口在 `tts_interface`：`~/text`（合成并按 `play` 播放）、`~/synthesize`（只合成）、
  `~/say`（长文本、逐句、可取消）、`~/event`（合成/播放/失败事件）。
- 默认节点全名 `/audio_tts`；多机器人时用命名空间 `/robot1/audio_tts`。
- `tts` 不感知鸭子语义；`duck_audio` 的 tag、seed、personality 不进入 `tts`。
- `tts` 内部是单工作线程调度：同组 `~/text` 最新覆盖、可打断 `~/say`，后端推理始终单飞；
  队列上限与抢占由 `scheduler.*` 参数控制。
- 后端不可用（模型缺失、paroli-server 未启动）只让 TTS 请求失败，节点和运动控制都不受影响。

---

## 8. ROS 2 接口、QoS 与生命周期

### 8.1 `audio_io` 接口、QoS 与生命周期

`audio_io` 的接口表、QoS、`start()` / `stop()` 语义、设备重试和状态字段已迁移到：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

### 8.2 各包的 lifecycle

`duck_audio` / `duck_ear` / `tts` / `asr` / `duck_asr` 都是组合式普通节点，lifecycle 设计仍待实现。`duck_audio` 的 voice 默认可用，特雷门等待 `~/theremin/enable`，合唱暂不提供；`tts` 由参数选择后端，后端不可用时节点保持运行但请求失败。

---

## 9. 设备与后端

设备后端的详细说明已迁移到：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

系统级只保留一条硬约束：

**PulseAudio、PipeWire、直接 ALSA 不要同时抢同一张声卡。** 机器人上如果 `audio_io` 直接使用 ALSA，必须停掉/禁用 PulseAudio，并确保 PipeWire 不接管该卡；否则会得到 `Device or resource busy`。

---

## 10. 实时预算

`audio_io` 的采样率、period/buffer、解码/欠载行为已迁移到：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

特雷门端到端、`wheee` 退出属于 `duck_audio`，见其文档；TTS 首句延迟（本机冷合成约 0.3 s、缓存命中约 0.1 s）见
[`src/tts/docs/tts-ros2-architecture.zh-CN.md`](../src/tts/docs/tts-ros2-architecture.zh-CN.md)。

---

## 11. 失败模式与降级

`audio_io` 的设备、解码、欠载、流超时等失败行为已迁移到：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

各包：

| 失败 | 行为 |
|---|---|
| 没有音库 | `duck_audio` 的 voice 模块禁用语义声音；theremin 没有 seed 时禁用；chorale 未实现；TTS/文件播放仍可用 |
| `duck_audio` 某个模块失败 | 该模块 `state=error`，其他模块继续；节点不 panic、不退出 |
| TTS 后端失败 | `Synthesize` 返回 `success=false`；topic/action 路径发 `~/event FAILED`；节点保持运行，不自动切换后端 |

---

## 12. `audio_io` 参数

`audio_io` 的参数默认值、launch 的 `config` / `namespace` 用法和参数文件 key 规则已迁移到：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

`voice.*`、`theremin.*`、`chorale.*` 属于 `duck_audio`；`tts` 节点的 `backend`（与 `audio_io` 的同名参数互不相关）、`piper.*`、`paroli.*`、`cache.*` 也不在 `audio_io` 中声明。

---

## 13. 包布局

```text
src/
  # audio_io_interface / audio_io 的完整布局见
  # src/audio_io/docs/audio_io-architecture.zh-CN.md

  tts_interface/
    msg/TtsText.msg
    msg/TtsEvent.msg
    srv/Synthesize.srv
    action/Say.action
    include/tts_interface/ros_msg.hpp
    python/tts_interface_msg/ros_msg.py
    test/

  tts/                   # 详细布局见 src/tts/docs/tts-ros2-architecture.zh-CN.md
    include/tts/...
    src/...
    config/tts.yaml
    launch/tts.launch.py
    tools/install_piper.sh
    docs/

  duck_audio_interface/
    srv/{PlayVoice,SetTheremin,SetChorale}.srv
    msg/{VoiceState,WheeeHold,ThereminState,ChoraleState,PettingEvent}.msg

  duck_audio/              # 详细布局见 src/duck_audio/docs/duck-ros2-architecture.zh-CN.md
    include/duck_audio/...
    src/...
    rust/                  # vendored sounds + C ABI
    config/duck_audio.yaml
    launch/duck_audio.launch.py
    tools/

  duck_ear/              # 详细布局见 src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md
    include/duck_ear/...
    src/...
    rust/                  # pet-detect + C ABI
    models/pet_detect.onnx
    config/duck_ear.yaml
    launch/duck_ear.launch.py

  asr_interface/   # 详细布局见 docs/duck-ros2-asr-architecture.zh-CN.md
    msg/{AsrEvent,AsrState}.msg
    include/asr_interface/ros_msg.hpp
    python/asr_interface_msg/ros_msg.py

  asr/            # 库 + 节点；不依赖任何 duck_* 包
    include/asr/...
    src/...
    config/asr.yaml
    launch/asr.launch.py
    tools/replay_capture.cpp

  duck_asr_interface/
    msg/DuckIntent.msg
    include/duck_asr_interface/ros_msg.hpp

  duck_asr/                   # 口令/意图；链接 libasr，不实现 ASR 后端
    include/duck_asr/...
    src/...
    config/{duck_commands,duck_asr}.yaml
    launch/duck_asr.launch.py
    tools/gen_vocabulary.py
```

---

## 14. 验收标准

`audio_io` / `audio_io_interface` 的验收标准、测试和运行验证已迁移到：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

各包：

1. `tts_node` 的 `~/synthesize`、`~/text`、`~/say`、`~/event` 可用；`piper` 与 `paroli` 后端可切换（已完成本机 piper 与 paroli 协议验证）；详见 [`src/tts/docs/tts-ros2-architecture.zh-CN.md`](../src/tts/docs/tts-ros2-architecture.zh-CN.md)。
2. `duck_ear` 在麦克风上检测摸头并发布 `PettingEvent`；`coo`/动作消费待接。详见 [`src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md`](../src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md)。
3. `duck_audio` 的 voice/wheee/特雷门可用，且单模块失败不影响其他模块；合唱暂不实现。
4. `duck_audio` 默认节点为 `/duck_audio`，`~/voice`、`~/theremin` 接口可用。
5. `asr` 的 `~/event` 能发布 `EVENT_WAKE`/`EVENT_FINAL`，播放期间门控；`duck_asr` 能查询电量并播报、
   发布姿势/动作 `DuckIntent`；姿势/动作执行待 `duck_control` 命令接口。`duck_asr_node` 默认同进程组合
   `libasr`，`asr.compose=false` 时可用独立 `asr_node`。详见
   [`docs/duck-ros2-asr-architecture.zh-CN.md`](./duck-ros2-asr-architecture.zh-CN.md)。

---

## 15. 迁移顺序

1. **`audio_io_interface` + `audio_io`（已完成）**：详见 [`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)。
2. **`audio_io` launch 集成（已完成）**：默认 `/audio_io`；支持外部 `config` 与可选 `namespace`。
3. **`duck_audio` 的 voice/wheee 模块（已完成）**：播放预渲染音库；启动 greet；`wheee` 用 `AudioStream`。
4. **`tts_interface` + `tts_node`（已完成）**：`synthesize`/`text`/`say`/`event`，piper 与 paroli 后端；板端 paroli + RKNN 待实机验证。细节见 [`src/tts/docs/tts-ros2-architecture.zh-CN.md`](../src/tts/docs/tts-ros2-architecture.zh-CN.md)。
5. **`audio_io` capture + `duck_ear`**：采集与摸头检测已完成；声音/动作消费待接。详见 [`src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md`](../src/duck_ear/docs/duck-ros2-ear-architecture.zh-CN.md)。
6. **`duck_audio` 的 theremin（已完成）/ chorale（待实现）**：特雷门按 `~/theremin/enable` 开启；合唱接口保留，当前节点不广告。
7. **AIC3104 板级适配**：overlay/DKMS 或换 USB 声卡；再切回机器人内置扬声器/麦克风。
8. **语音识别（设计）**：`asr_interface` + `asr`（mock 后端 → 真实后端）→
   `duck_asr_interface` + `duck_asr`（链接 `libasr` 同进程组合；口令/电量/TTS）→ 接 `duck_control`
   命令接口执行动作。详见
   [`docs/duck-ros2-asr-architecture.zh-CN.md`](./duck-ros2-asr-architecture.zh-CN.md)。

---

## 16. 与运动控制架构的边界

- `ros2_control` / `DuckPolicyController` 负责 50 Hz 运动闭环；**不读音频、不写音频**。
- `duck_audio` 的 theremin（未来 chorale）需要动嘴/动头时，通过控制层的命令接口发送目标，不直接写舵机。
- `audio_io` 不感知机器人语义；它只认 `AudioClip`、`AudioStream`、优先级和组。
- `asr` / `duck_asr` 不进入 50 Hz 控制回路；识别结果只以 `DuckIntent` 交给行为/控制节点，由后者做安全仲裁后写控制接口。
- 音频节点崩溃不应导致运动控制崩溃；反之，控制层重启不应丢失音库或 TTS 缓存。

---

## 附录 A：当前环境测试命令

### A.1 开发板

```bash
# 声卡与采集设备
cat /proc/asound/cards
aplay -l
arecord -l
ls -l /dev/snd

# 谁占着设备
fuser -v /dev/snd/*

# PipeWire / PulseAudio 状态
pactl info
pactl list short sinks
pactl list short sources
pw-cli ls Device
pw-cli ls Node
pw-play --list-targets

# 直接 ALSA 测试（需先停掉 PulseAudio，避免 EBUSY）
aplay -D plughw:CARD=<card>,DEV=0 /tmp/test.wav
arecord -D plughw:CARD=<card>,DEV=0 -f S16_LE -r 16000 -c 1 -d 3 /tmp/mic.wav
```

### A.2 audio_io 本地验证

`audio_io` 的 launch、外部配置、namespace、麦克风和 MP3 验证命令见：

**[`src/audio_io/docs/audio_io-architecture.zh-CN.md`](../src/audio_io/docs/audio_io-architecture.zh-CN.md)**

### A.3 笔记本 PipeWire

```bash
pactl info
pw-play --list-targets
pw-play /tmp/test.wav
pactl list sinks | grep -E "状态|State|名称|Name"
```

### A.4 启用 PipeWire（开发板，可选）

```bash
sudo apt install pipewire-audio-client-libraries pipewire-pulse wireplumber
systemctl --user disable --now pulseaudio.service pulseaudio.socket
systemctl --user enable --now pipewire pipewire-pulse wireplumber
sudo loginctl enable-linger cat
wpctl status
pw-play /tmp/test.wav
```

注意：这会让当前桌面音频从 PulseAudio 切到 PipeWire；生产机器人如果选 ALSA 直连，则保持 PulseAudio/PipeWire 不占用设备。

---

## 附录 B：待确认问题

1. `microduck_rev` 的机器人最终使用内置 AIC3104，还是 USB 声卡？这决定 `playback.device` 的默认值和是否需要板级 overlay。
2. 是否允许 TTS 与音效同时出声？当前 `tts` 固定 `SPEECH` + `REPLACE_SAME_GROUP`/`QUEUE`，不做 `MIX`；如果后续要混音，`MIX` 的 ducking 曲线需要定。
3. `duck_audio` 内部的 theremin / chorale 是否始终同进程，还是保留按参数禁用/拆回独立节点的能力？取决于实测端到端延迟和稳定性。
4. 音库 seed 是否继续绑定 SoC serial，还是允许每台机器人手工配置？
5. WebRTC 双向音频是否在近期进入范围？如果进入，`audio_io` 需要增加 `AudioStream` 的 jitter buffer 和回声消除策略。
6. 语音识别后端选 `sherpa-onnx` 还是 `vosk`？唤醒词用现成中文 KWS 模型还是自定义训练？需要板端实测 CPU、延迟、丢包后再定。
7. 姿势/动作口令是否必须唤醒后执行？App/按键触发（`push_to_talk`）是否也要作为正式入口？
