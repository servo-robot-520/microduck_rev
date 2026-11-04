# duck_audio

microduck 的 ROS 2 语义声音节点。它复用原版 Rust `sounds` crate 的合成、音色身份和
`Stream`，不重新实现 DSP；播放设备仍然只归 `audio_io`。

当前实现范围：

- 一次性语义音效：`alarm/greet/inquire/peck/chirp/coo`
- 按住式 `wheee`：start → loop → end
- Gemini 特雷门：深度图/点云 → `closeness` → `Stream::wheee`
- 嘴部目标输出 `~/mouth`
- **合唱暂不实现**：`SetChorale`/`ChoraleState` 接口保留，但节点不广告 `~/chorale/enable`

## 依赖

- ROS 2 Humble、C++23（GCC 13+）
- `rclcpp`、`sensor_msgs`、`std_msgs`、`geometry_msgs`、`tf2_ros`、`tf2_sensor_msgs`
- `audio_io_interface`、`duck_audio_interface`
- Rust 1.89+、Cargo（构建 `libmicroduck_sounds.so`）
- 运行期：`audio_io`、Orbbec `orbbec_camera`（可选，特雷门才需要）

## 目录

```text
duck_audio/
├── include/duck_audio/        # 组合式节点、voice/wheee/theremin、FFI 头
├── src/                       # 实现；main.cpp 只负责创建节点和 spin
├── rust/
│   ├── sounds/                # 上游固定 commit 的副本
│   ├── robotd-params/         # 最小 serial 兼容层
│   ├── microduck_sounds_ffi/  # C ABI，产物 libmicroduck_sounds.so
│   └── UPSTREAM_MICRODUCK_COMMIT
├── config/duck_audio.yaml
├── launch/duck_audio.launch.py
├── test/                      # hand tracker、WAV reader、audio session
├── tools/                     # 上游同步、FFI 构建测试
└── docs/                      # 架构、FFI、Gemini 特雷门文档
```

## 构建

```bash
source /opt/ros/humble/setup.bash
colcon build --base-paths src \
  --packages-select audio_io_interface duck_audio_interface duck_audio \
  --cmake-args -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13
```

Rust 库在 CMake 中通过 `cargo build --release --locked -p microduck_sounds_ffi` 构建，
产物安装为 `lib/libmicroduck_sounds.so`。也可以单独构建/测试 FFI：

```bash
src/duck_audio/tools/build_ffi.sh
```

上游 `sounds` 的同步：

```bash
src/duck_audio/tools/sync_microduck_sounds.sh          # 固定 commit
src/duck_audio/tools/sync_microduck_sounds.sh --latest # 拉取当前 origin/HEAD
```

## 运行

`audio_io` 必须先启动（没有声卡时可用 `backend=null`）。特雷门还需要相机：

```bash
ros2 launch orbbec_camera gemini_330_series.launch.py \
  camera_name:=camera enable_color:=false enable_depth:=true \
  depth_registration:=false enable_point_cloud:=false \
  depth_qos:=sensor_data depth_camera_info_qos:=sensor_data depth_fps:=15

ros2 launch duck_audio duck_audio.launch.py
```

默认配置见 `config/duck_audio.yaml`。音库由安装步骤渲染：

```bash
sounds ensure-bank --dir /var/lib/robot/sounds
```

节点只读取并校验音库，不在启动时重新渲染。

## 接口

节点全名 `/duck_audio`，服务/话题相对于该命名空间。

```bash
# 一次性声音；variant=0xFFFFFFFF 表示随机
ros2 service call /duck_audio/voice/play \
  duck_audio_interface/srv/PlayVoice "{tag: chirp, variant: 4294967295}"

# 特雷门
ros2 service call /duck_audio/theremin/enable \
  duck_audio_interface/srv/SetTheremin "{active: true}"
ros2 topic echo /duck_audio/theremin/state

# wheee：按住时 5–10 Hz 发 held=true；松开发 false
ros2 topic pub /duck_audio/voice/wheee_hold \
  duck_audio_interface/msg/WheeeHold "{held: true}"

ros2 topic echo /duck_audio/voice/state
ros2 topic echo /duck_audio/mouth
```

发布到 `audio_io`：

- `/audio_io/play`：`AudioClip`，一次性音效
- `/audio_io/stream`：`AudioStream`，`wheee` 与特雷门，48 kHz mono s16le

## 组合式使用

`DuckAudioNode` 不继承 `rclcpp::Node`，可以复用调用方的节点：

```cpp
auto node = std::make_shared<rclcpp::Node>("duck_audio");
auto audio = std::make_shared<duck_audio::DuckAudioNode>(node);

rclcpp::executors::MultiThreadedExecutor executor;
executor.add_node(node);
executor.spin();
```

CMake 输出：

| target | 类型 | 说明 |
|---|---|---|
| `duck_audio` | `libduck_audio.so` | 节点实现、voice/wheee/theremin、FFI |
| `duck_audio_node` | executable | 只含 `main.cpp`，链接 `duck_audio` |

## 测试

```bash
colcon test --base-paths src --packages-select duck_audio
colcon test-result --verbose
```

当前覆盖 hand tracker、WAV 音库读取和音频通道仲裁。FFI 另有 `rust/test/ffi_smoke.cpp`
和 `tools/build_ffi.sh`。

## 已知限制

- 合唱未实现；节点不广告合唱服务/状态。
- 摸头触发 `coo` 依赖后续 `duck_ear`；当前可由外部 `PlayVoice` 请求。
- `wheee` 三段 WAV 缺失时拒绝启动 wheee，不做一次性降级。
- 深度图输入不支持 TF；点云可通过 `theremin.target_frame` 变换。
- 相机/音频设备是可选设备；失败只影响声音，不影响运动控制。
