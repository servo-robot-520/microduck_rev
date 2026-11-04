# duck_ear

麦克风感知节点：订阅 `audio_io` 的采集流，做重采样和 log-mel，然后用原版
`pet-detect` CNN 判断是否在摸头，发布 `duck_audio_interface/PettingEvent`。

**本节点不做动作。** 摸头后的声音反馈由 `duck_audio` 消费事件；运动动作由行为/控制节点消费，
不直接写舵机。

## 当前实现

| 项 | 状态 |
|---|---|
| `/audio_io/capture` 订阅 | 已实现 |
| 48 kHz / 任意采样率 → 16 kHz mono | 已实现，线性重采样，跨块连续 |
| pet-detect log-mel + CNN | 复用原版 Rust `pet-detect`，模型 `models/pet_detect.onnx` |
| `~/petting` `PettingEvent` | 已实现（Start/End + confidence） |
| 环境声 `SoundEvent` | 暂未发布 |
| 动作控制 | 不在本节点；由行为/控制节点消费事件 |

`libduck_ear.so` 是 C++ 库，内部链接 Rust `duck_ear_ffi.a`。Rust 侧通过
`ort` 的 `load-dynamic` 加载 ONNX Runtime；**运行时需要目标机上有 `libonnxruntime.so`**。
没有该库或模型时，节点会记录错误并保持运行，只是不发布摸头事件。

## 构建

```bash
source /opt/ros/humble/setup.bash
colcon build --base-paths src \
  --packages-select audio_io_interface duck_audio_interface duck_ear \
  --cmake-args -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13
```

CMake 输出：

```text
lib/libduck_ear.so
lib/duck_ear/duck_ear_node
share/duck_ear/models/pet_detect.onnx
```

## 运行

```bash
ros2 launch audio_io audio_io.launch.py
ros2 launch duck_ear duck_ear.launch.py
```

采集侧必须发布 `AudioStream`，默认：

```text
topic      /audio_io/capture
stream_id  mic
encoding   s16le
```

输出：

```bash
ros2 topic echo /duck_ear/petting
```

消息：

```text
uint8 event       # PettingEvent.START / END
float32 confidence
```

`enter_threshold=0.95`、`exit_threshold=0.85` 是原版滞回；`stride: 0` 使用约 250 ms 的默认
推理间隔。

## 测试

```bash
colcon test --base-paths src --packages-select duck_ear
colcon test-result --verbose
```

当前测试覆盖 48 kHz → 16 kHz 跨块重采样和 16 kHz 直通。

## 消费关系

```text
/audio_io/capture → duck_ear → /duck_ear/petting
                               ├→ duck_audio: 播 coo（可选）
                               └→ 行为/控制节点: 特定动作（可选）
```

`duck_ear` 只发布事件，不直接调用 `duck_audio` 或控制层。
