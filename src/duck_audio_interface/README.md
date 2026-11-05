# duck_audio_interface

## 简介

`duck_audio` 的 ROS 2 语义接口。它只定义消息/服务，不实现节点，也不依赖 `audio_io`。

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
| `~/chorale/enable` | `srv/SetChorale` | 启停合唱、选择曲目（接口保留，当前 `duck_audio` 暂不广告） |
| `~/chorale/state` | `msg/ChoraleState` | 声部、beat、人数（接口保留，当前不发布） |
| `~/petting/event` | `msg/PettingEvent` | `duck_ear` 的摸头 Start/End 事件 |

`wheee` 不放进 `PlayVoice`：它需要持续按住和松手落地，用 `WheeeHold` 表达电平，而不是
一次服务调用。

## 示例

```bash
ros2 service call /duck_audio/voice/play \
  duck_audio_interface/srv/PlayVoice "{tag: chirp, variant: 4294967295}"

ros2 service call /duck_audio/theremin/enable \
  duck_audio_interface/srv/SetTheremin "{active: true}"

# 合唱接口保留，当前 duck_audio 不广告 ~/chorale/enable。
# ros2 service call /duck_audio/chorale/enable \
#   duck_audio_interface/srv/SetChorale "{active: true, piece: 1}"

ros2 topic echo /duck_audio/voice/state
ros2 topic echo /duck_audio/theremin/state
```

`variant: 4294967295`（`0xFFFFFFFF`）表示由 `duck_audio` 随机挑一个变体。

## 构建

```bash
colcon build --base-paths src --packages-select duck_audio_interface
```

## C++ / Python 辅助

- `include/duck_audio_interface/ros_msg.hpp`：节点私有 endpoint 常量、外部绝对名称 helper、生成类型别名。
- `python/duck_audio_interface_msg/ros_msg.py`：同一组 endpoint 名称 helper，供 Python 客户端使用。
- 每个 helper 还接受可选的第二个 namespace 参数，内部调用 `qualify_node_fqn()`；例如
  `service_play_voice(kDefaultNodeFqn, node->get_namespace())` 得到 `<namespace>/duck_audio/voice/play`。
- `cmake_include.cmake`：供 CLion 源码聚合构建直接使用 in-tree typesupport target。

```cpp
#include "duck_audio_interface/ros_msg.hpp"

auto play = duck_audio_interface::service_play_voice("/robot_a/duck_audio");
auto state = duck_audio_interface::topic_voice_state();
```
