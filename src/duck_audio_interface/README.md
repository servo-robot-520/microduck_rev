# duck_audio_interface

`duck_audio` 的 ROS 2 语义接口。它只定义消息/服务，不实现节点，也不依赖 `audio_io`。

## 为什么单独一个包

`audio_io_interface` 是通用音频层，不应该知道 `chirp`、`coo`、`wheee`、seed 或声部。
`duck_audio_interface` 让 App、`robotctl`、行为节点通过语义名字请求声音，而不是依赖
`duck_audio` 的内部实现或直接拼 `AudioClip.uri`。

## 接口

| 接口 | 类型 | 用途 |
|---|---|---|
| `~/voice/play` | `srv/PlayVoice` | `alarm/greet/inquire/peck/chirp/coo` 一次性声音 |
| `~/voice/wheee_hold` | `msg/WheeeHold` | `wheee` 的按住信号，5–10 Hz lease |
| `~/voice/state` | `msg/VoiceState` | 音库、seed、状态、最近错误 |
| `~/theremin/enable` | `srv/SetTheremin` | 启停特雷门 |
| `~/theremin/state` | `msg/ThereminState` | 距离、closeness、音高、嘴、传感器状态 |
| `~/chorale/enable` | `srv/SetChorale` | 启停合唱、选择曲目 |
| `~/chorale/state` | `msg/ChoraleState` | 声部、beat、人数 |

`wheee` 不放进 `PlayVoice`：它需要持续按住和松手落地，用 `WheeeHold` 表达电平，而不是
一次服务调用。

## 示例

```bash
ros2 service call /duck_audio/voice/play \
  duck_audio_interface/srv/PlayVoice "{tag: chirp, variant: 4294967295}"

ros2 service call /duck_audio/theremin/enable \
  duck_audio_interface/srv/SetTheremin "{active: true}"

ros2 service call /duck_audio/chorale/enable \
  duck_audio_interface/srv/SetChorale "{active: true, piece: 1}"

ros2 topic echo /duck_audio/voice/state
ros2 topic echo /duck_audio/theremin/state
```

`variant: 4294967295`（`0xFFFFFFFF`）表示由 `duck_audio` 随机挑一个变体。

## 构建

```bash
colcon build --base-paths src --packages-select duck_audio_interface
```
