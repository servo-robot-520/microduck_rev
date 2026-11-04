# duck_audio：Gemini 特雷门

> 目标：`microduck_rev` 没有原型的 VL53L5CX ToF，只有 Orbbec Gemini（深度 + 点云）。本文说明如何用 Gemini 产生原特雷门需要的 `closeness 0..1`，并接到原版 Rust `Stream::wheee`。
> 相关：[duck_audio 架构](duck-ros2-architecture.zh-CN.md)、[Rust FFI 边界](duck-ros2-rust-ffi.zh-CN.md)、[原型合成原理](microduck-sounds-synthesis.zh-CN.md)。

---

## 0. 结论

**不需要改 Rust 合成器。** 原特雷门的输入是：

```text
手在可玩范围内的距离 → closeness(0 远, 1 近)
                     → hz = Stream::hz_at(closeness)
                     → level = 1 / 0
                     → open = closeness（嘴）
```

Gemini 只是换掉“距离从哪来”：

```text
Gemini 深度图/点云 → ROI 裁剪 → 最近的有支撑深度簇 → range_m → closeness
```

推荐顺序：

1. **优先用 `/camera/depth/image_raw`（16UC1，毫米）**：数据量小、天然按像素组织、ROI 简单、单位明确。
2. 如果点云已经被其他节点使用，或需要 3D 空间筛选，再用 `/camera/depth/points`（XYZ，米）。
3. 不要用彩色图做手部识别；本功能只需要深度。

原 ToF 是 8×8 区域、带状态字节、量程短；Gemini 是 dense depth、量程长、没有“手部状态”。因此算法要补上原 `kinematics::hand` 的两件事：**最小支撑点数**和**250 ms hold**。

---

## 1. 原 ToF 与 Gemini 的差异

| 维度 | 原 VL53L5CX/L8CX | Gemini 深度/点云 |
|---|---|---|
| 形态 | 8×8 区域，每区域一个距离 + 状态 | 640×480 左右 dense depth；点云每像素 XYZ |
| 单位 | mm | 深度图 mm；点云 m |
| 量程 | 短（默认 playable 0.10–0.70 m） | 远得多；需要自己裁 near/far |
| 无效值 | 状态字节 255、负距离、低置信 | 0、NaN、无效点；点云默认过滤 |
| 背景 | 视场内所有回波，含墙/地板 | 同样含墙/地板/机器人自身 |
| 频率 | 15 Hz | 15/30 Hz 可配 |
| 坐标系 | 传感器 8×8 光束 | 光学系：x 右、y 下、z 前；或 TF 到 body |
| 关键噪声 | 近距 crosstalk、远处 consistency failed | 深度孔洞、反光/透明、边缘飞点、阳光 IR |

原 ToF 最终版**不做背景扣除**：显式打开特雷门后，范围内最近的返回就是手；对着墙会持续响，这是有意行为。Gemini 沿用同一语义：**最近的有足够支撑的深度簇就是手**。这样行为可预测，也不需要“armed 背景”这种在噪声上做二次推理的机制。

---

## 2. Orbbec 数据模型

### 2.1 话题

以 `camera_name:=camera` 为例：

| 话题 | 类型 | 单位/内容 |
|---|---|---|
| `/camera/depth/image_raw` | `sensor_msgs/Image`，`16UC1` | 深度，**毫米** |
| `/camera/depth/camera_info` | `sensor_msgs/CameraInfo` | 深度内参、frame id |
| `/camera/depth/points` | `sensor_msgs/PointCloud2`，`x/y/z` float32 | **米**，光学系 |
| `/camera/depth_registered/points` | `PointCloud2` | 彩色对齐点云，需 `enable_colored_point_cloud` |
| `/tf`、`/tf_static` | TF | `camera_link` → `camera_depth_optical_frame` 等 |

点云发布条件：`enable_point_cloud:=true`；`depth_registered/points` 还需要彩色。深度图始终由 depth stream 发布。

### 2.2 单位

Orbbec 的深度图在发布前做了：

```cpp
auto depth_scale = depth_frame->getValueScale();
image = image * depth_scale;
```

所以 Y16 深度图的值是**毫米**，不是米。点云生成时：

```cpp
*iter_z = points[i].z / 1000.0;   // 米
```

所以 `PointCloud2` 是米。**两种输入的阈值不能共用同一个单位假设**；代码里用 `depth_mm * 0.001` 或直接读点云米。若更换相机型号或 `depth_format`，先用 `camera_info` 与实际距离核对 `depth_scale`，不要把这里的结论当成所有 Orbbec 型号的常量。

### 2.3 frame id 与 registration

`depth_registration` 默认 `true`。它会影响 frame id：

```text
depth_registration = false → depth/points 在 camera_depth_optical_frame
depth_registration = true  → depth 对齐到 color，点云在 camera_color_optical_frame
```

如果只关心深度、不关心彩色，建议：

```text
depth_registration:=false
enable_color:=false
```

这样：

- 不需要彩色帧参与；
- 深度图/点云在 `camera_depth_optical_frame`；
- 少一次对齐计算，少一份 USB 带宽。

如果保留 `depth_registration:=true`，必须按 `camera_color_optical_frame` 使用点云，并且彩色流要可用；否则对齐/彩色点云可能没有输出或语义不对。

`cloud_frame_id` 可以覆盖点云 frame id；一般留空。

### 2.4 光学系方向

`camera_depth_optical_frame` 遵循 REP 103 光学系：

```text
x → 右
y → 下
z → 前（相机朝向）
```

因此：

- **前后距离用 `z`**（不是欧氏 `sqrt(x²+y²+z²)`；后者会让手横向移动时音高变化）。
- 左右用 `x`，上下用 `y`；它们用于 ROI 和排除地板/机器人自身。
- 地板在 `y` 正方向（下方）；如果相机俯仰，地面点会进入 ROI，必须用 `y` 上限或 TF 到 body 后裁掉。

### 2.5 QoS

Orbbec 的 QoS 由参数决定：

| 参数 | 默认 | 建议 |
|---|---|---|
| `point_cloud_qos` | `default`（reliable） | `sensor_data`（best effort） |
| `depth_qos` | `default` | `sensor_data` |
| `depth_camera_info_qos` | `default` | `sensor_data` |
| `color_qos` | `default` | 不用彩色则无关 |

订阅端必须用兼容 QoS：

```cpp
rclcpp::SensorDataQoS qos; // best_effort, depth 5
```

如果 publisher 是 reliable、subscriber 是 best_effort，通常兼容；反过来不一定。**统一在 launch 里设为 `sensor_data`，订阅端也用 `SensorDataQoS`**，不要靠默认值猜。

### 2.6 推荐 launch

#### 方案 A：深度图（推荐）

```bash
ros2 launch orbbec_camera gemini_330_series.launch.py \
  camera_name:=camera \
  enable_color:=false \
  enable_depth:=true \
  depth_registration:=false \
  enable_point_cloud:=false \
  depth_qos:=sensor_data \
  depth_camera_info_qos:=sensor_data \
  depth_fps:=15
```

#### 方案 B：点云

```bash
ros2 launch orbbec_camera gemini_330_series.launch.py \
  camera_name:=camera \
  enable_color:=false \
  enable_depth:=true \
  depth_registration:=false \
  enable_point_cloud:=true \
  ordered_pc:=false \
  point_cloud_qos:=sensor_data \
  depth_qos:=sensor_data \
  depth_fps:=15
```

说明：

- `ordered_pc:=false`：点云只保留有效点，`width=valid_count, height=1, is_dense=true`；没有像素网格，但数据量小。
- `ordered_pc:=true`：保留 `width×height` 网格，无效点为 0；适合按像素 ROI，但数据量大。
- `enable_point_cloud` 默认 `false`；不显式打开就没有 `/camera/depth/points`。
- `depth_fps:=0` 是自动；显式 15 降低 CPU/带宽，30 提高响应。特雷门 15 Hz 已接近原 ToF 的帧率。
- `enable_noise_removal_filter` 默认 true；可保留。`enable_temporal_filter` 会引入额外延迟，默认关闭；如果深度抖动明显再评估。
- `enable_decimation_filter` 可减点；但会降低手部支撑点数，需同步调 `min_points`。

---

## 3. 推荐方案：深度图优先

| 维度 | `depth/image_raw` | `depth/points` |
|---|---|---|
| 数据量 | 640×480×2 ≈ 0.6 MB/帧 | 有效点 × 12 B；全图约 3.7 MB |
| 解析 | 按行/像素，ROI 是矩形 | 逐点迭代，需处理 `point_step/row_step` |
| 单位 | mm（明确） | m（明确） |
| 无效点 | 0 | 已过滤或网格 0 |
| 适合 | 手部 ROI、深度直方图 | 3D 空间筛选、与其他点云消费者共享 |
| CPU | 低 | 高 |

**结论**：特雷门只需要“前方一个 ROI 里的深度”，深度图更合适。点云不是不能用；它只是更重，而且 `ordered_pc=false` 后没有像素网格，ROI 只能靠 XYZ 坐标筛。

如果机器人上已有导航/避障节点需要点云，可以让它们订阅同一个 `/camera/depth/points`；`duck_audio` 仍优先用深度图，避免重复解析大点云。

---

## 4. 手部跟踪算法

### 4.1 输入抽象

```cpp
struct HandSample {
    double range_m;      // 最近有支撑的深度
    double closeness;    // 0 远, 1 近
    std::size_t points;  // 支撑点数/像素数
    bool held;           // 是否是 hold 的上一帧
};

class HandSource {
public:
    virtual ~HandSource() = default;
    virtual std::optional<HandSample> latest(std::chrono::steady_clock::time_point now) = 0;
    virtual bool healthy() const = 0;
    virtual void reset() = 0;
};
```

`GeminiDepthSource` 和 `GeminiPointCloudSource` 实现同一个接口；`ThereminModule` 不关心输入形态。

### 4.2 参数

| 参数 | 默认 | 含义 |
|---|---|---|
| `near_m` | 0.12 | 最近可玩距离；低于此视为噪声/自身 |
| `far_m` | 0.70 | 最远可玩距离；超出不产生音符 |
| `lateral_m` | 0.35 | 点云左右裁剪半宽；超过视为不在手部区域 |
| `y_min_m` / `y_max_m` | −0.25 / +0.20 | 点云上下裁剪；排除地板/头顶 |
| `roi_x_frac` | 0.60 | 深度图水平 ROI 占全宽比例（居中） |
| `roi_y0_frac` / `roi_y1_frac` | 0.20 / 0.80 | 深度图垂直 ROI 起止比例；排除地面/天空 |
| `min_points` | 40（点云）/ 200（深度图像素） | 一个深度簇的最小支撑 |
| `bin_m` | 0.02 | 深度直方图 bin 宽 |
| `hold_ms` | 250 | 丢帧保持；与原 ToF 一致 |
| `stale_ms` | 200 | 输入 watchdog；超过则视为无手 |
| `smooth_tau_ms` | 0（可选） | 额外 EMA；默认交给 `Stream` 的 45 ms pitch slew |

`near_m`/`far_m` 是**可玩范围**，不是相机量程。Gemini 能看更远，但特雷门只在这个区间发声。`lateral_m`/`y_*_m` 只用于点云；深度图用 `roi_*_frac` 裁像素。

### 4.3 点云处理

```cpp
std::optional<HandSample> GeminiPointCloudSource::process(
    const sensor_msgs::msg::PointCloud2& cloud,
    std::chrono::steady_clock::time_point now) {
    if (cloud.point_step < 12 || cloud.width == 0) return hold(now);

    std::vector<float> zs;
    zs.reserve(std::min<std::size_t>(cloud.width * cloud.height, 200000));

    sensor_msgs::PointCloud2ConstIterator<float> x(cloud, "x");
    sensor_msgs::PointCloud2ConstIterator<float> y(cloud, "y");
    sensor_msgs::PointCloud2ConstIterator<float> z(cloud, "z");
    for (; x != x.end(); ++x, ++y, ++z) {
        if (!std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) continue;
        if (*z < cfg_.near_m || *z > cfg_.far_m) continue;
        if (std::abs(*x) > cfg_.lateral_m) continue;
        if (*y < cfg_.y_min_m || *y > cfg_.y_max_m) continue;
        zs.push_back(*z);
    }
    return cluster(zs, now);
}
```

注意：

- `PointCloud2ConstIterator` 已经处理 `point_step`/`row_step`/字段偏移；不要手写 `reinterpret_cast` 遍历。
- `x.end()` 是 `x` 自己的 end；三个迭代器同步前进。
- `ordered_pc=false` 时 `height=1`，`width=valid_count`；仍有 `x.end()`。
- 点云中可能包含机器人自身；`near_m` 和上下/左右裁剪是第一道防线。相机安装位置确定后，用实际点云观察并调 `y_min/y_max`。

### 4.4 深度图处理

```cpp
std::optional<HandSample> GeminiDepthSource::process(
    const sensor_msgs::msg::Image& image,
    std::chrono::steady_clock::time_point now) {
    if (image.encoding != sensor_msgs::image_encodings::TYPE_16UC1) return hold(now);
    if (image.step < image.width * sizeof(uint16_t)) return hold(now);

    const int x0 = image.width  * (1 - cfg_.roi_x_frac) / 2;
    const int x1 = image.width  - x0;
    const int y0 = image.height * cfg_.roi_y0_frac;
    const int y1 = image.height * cfg_.roi_y1_frac;

    std::vector<float> zs;
    for (int v = y0; v < y1; ++v) {
        const auto* row = image.data.data() + v * image.step;
        for (int u = x0; u < x1; ++u) {
            uint16_t mm;
            std::memcpy(&mm, row + u * sizeof(uint16_t), sizeof(mm));
            if (mm == 0) continue;
            const double z = mm * 0.001;
            if (z < cfg_.near_m || z > cfg_.far_m) continue;
            zs.push_back(static_cast<float>(z));
        }
    }
    return cluster(zs, now);
}
```

深度图的 ROI 是矩形像素区域，不需要 x/y 米制裁剪；但相机俯仰会带入地面，仍要调整 `roi_y1_frac`。如果相机固定且只对前方，矩形 ROI 足够。

### 4.5 最近有支撑的深度簇

为什么不是“取最小 z”或“取 20% 分位”：

- 点云/深度图里单点飞点很常见；最小 z 会变成噪声。
- 如果手只占 ROI 的 1%，而墙占 99%，全体 20% 分位会取到墙，手消失。
- 需要的是**最近的、有足够支撑的深度层**。

```cpp
std::optional<HandSample> cluster(const std::vector<float>& zs,
                                  std::chrono::steady_clock::time_point now) {
    if (zs.size() < cfg_.min_points) return hold(now);

    const int bins = std::max(1, static_cast<int>((cfg_.far_m - cfg_.near_m) / cfg_.bin_m));
    std::vector<std::size_t> count(bins, 0);
    std::vector<int> index;
    index.reserve(zs.size());

    for (float z : zs) {
        int b = static_cast<int>((z - cfg_.near_m) / cfg_.bin_m);
        b = std::clamp(b, 0, bins - 1);
        ++count[b];
        index.push_back(b);
    }

    // 最近的、达到 min_points 的 bin。
    int chosen = -1;
    for (int b = 0; b < bins; ++b) {
        if (count[b] >= cfg_.min_points) { chosen = b; break; }
    }
    if (chosen < 0) return hold(now);

    // 取该 bin（以及相邻 bin）的中位数，避免 bin 边界抖动。
    std::vector<float> picked;
    picked.reserve(count[chosen] * 3);
    for (std::size_t i = 0; i < zs.size(); ++i) {
        if (std::abs(index[i] - chosen) <= 1) picked.push_back(zs[i]);
    }
    const auto mid = picked.begin() + picked.size() / 2;
    std::nth_element(picked.begin(), mid, picked.end());
    const double range = *mid;
    const double span = std::max(cfg_.far_m - cfg_.near_m, 1e-6);
    const double closeness = std::clamp((cfg_.far_m - range) / span, 0.0, 1.0);

    last_ = {range, closeness, picked.size(), false};
    last_at_ = now;
    return last_;
}
```

也可以更简单：对 `zs` 排序，找第一个“连续 `min_points` 个点都在 `bin_m` 内”的窗口；但直方图更容易解释和调参。

### 4.6 hold 与 staleness

```cpp
std::optional<HandSample> hold(std::chrono::steady_clock::time_point now) {
    if (!last_) return std::nullopt;
    if (now - last_at_ > cfg_.hold) { last_.reset(); return std::nullopt; }
    auto held = *last_;
    held.held = true;
    return held;
}
```

- 输入帧本身有 watchdog：`last_cloud_at_` 超过 `stale_ms` 时，`ThereminModule` 直接 `tracker.reset()`、level 0；不要用 hold 把“相机断了”当成“手还在”。
- hold 只覆盖“这一帧没找到手”，不覆盖“没有帧”。
- 手真的离开后，250 ms 内仍会响；这是原型的 anti-chop 行为。更短会切碎，更长会拖尾。

### 4.7 映射到声音

```cpp
// last_hz 是模块状态：无手时保持最后有效音高。
const auto hand = source.latest(now);
if (hand) {
    const double hz = duck_hz_at(voice, hand->closeness);
    last_hz = hz;
    duck_stream_set(stream, hz, 1.0, hand->closeness);
    publish_mouth(hand->closeness);
    publish_state(hand, hz);
} else {
    // 保持最后音高、闭嘴、淡出；不要滑向最低音。
    duck_stream_set(stream, last_hz, 0.0, 0.0);
    publish_mouth(0.0);
}
```

- `duck_hz_at` 是原版的几何映射：`lo * (hi/lo)^position`，`lo=center*0.85`，`hi=center*(1.6+0.5*spread)`。不要用线性赫兹映射。
- `open=closeness`：手越近，音越高，嘴张得越大；一个手势三个输出。
- `level=0` 后由 `Stream` 的 30 ms level slew 淡出；不要在外层做 cut。
- `level` 回到 1 时，`Stream` 从当前相位继续；不需要重新创建 `DuckStream`，否则会有音高跳变。

---

## 5. 频率、延迟与线程

```text
Gemini depth: 15 Hz (67 ms/frame)
Theremin control: 50 Hz (20 ms)
PCM writer: 100 Hz (10 ms)
Stream pitch slew: 45 ms
Stream level slew: 30 ms
```

- 相机帧率低不是问题：`Stream` 的参数 slew 会把 15 Hz 的阶梯变成连续滑音。原 ToF 也是 15 Hz。
- 不要在 100 Hz writer 里解析点云；解析在相机回调/独立线程，writer 只读最新 `Hand`。
- `Hand` 用 `std::mutex` 或 `realtime_tools::RealtimeBuffer` 传给 50 Hz timer；临界区只复制几个 double。
- 相机回调用独立 callback group；`MutuallyExclusive` 即可（只有一路相机），但不要和 `voice/play` 服务共用默认 group。
- 如果点云解析超过 20 ms，考虑：
  1. 改深度图；
  2. `enable_decimation_filter` 或降低分辨率；
  3. 在独立线程解析，回调只入队最新 `shared_ptr`（丢旧帧）。

**延迟预算**：相机曝光/处理 + 传输 + 解析 + 50 Hz tick + Stream 45 ms slew。目标“手移动 → 听到变化” < 100 ms；实测用 `header.stamp` 与 `~/theremin/state` 对照。若超过 150 ms，先查相机帧率和 DDS，而不是改 `Stream` 的 slew（那会让滑音变钝）。

---

## 6. 坐标与 TF

### 6.1 相机固定、朝向嘴前方

如果 Gemini 固定在头部且光轴大致与嘴同向：

- 直接用 `camera_depth_optical_frame` 的 `z`。
- `x` 左右、`y` 上下用于 ROI。
- 不需要 TF，延迟最低。

### 6.2 相机随头部运动

如果相机装在会动的头上，`z` 是“相机前方”，不是“身体前方”：

- 订阅 `/tf`，把点变换到 `base_link` 或 `trunk`，再用 body 的前向轴做 ROI 和距离。
- 变换每个点成本高；如果只需要距离，可先把相机光轴投到 body，或使用 `tf2_sensor_msgs::doTransform`（会复制整片点云，尽量避免 15 Hz 全量转换）。
- 更简单的做法：用相机系 `z` 发声，但在头部大幅俯仰时接受音高随姿态变化；特雷门本来就是“手在嘴前”的乐器，头部动，乐器也跟着动，这未必是错。
- 若使用 `target_frame`，TF 查询失败时**不要猜**：本帧丢弃，`sensor_ok=false`；连续失败则静音。

### 6.3 安装与近场

- 相机可能看到自己的嘴/头壳：`near_m` 之外再用 `y`/`x` 裁剪；不要只看 `z`。
- 如果相机在嘴后方，`near_m` 要大于嘴到相机的距离；否则自己的嘴会成为“最近的手”。
- 标定时用 `ros2 topic echo /duck_audio/theremin/state` 看 `range_m/points`，把手放在已知距离，确认没有把机身算进去。

---

## 7. 标定流程

1. 启动相机（方案 A 或 B），确认：

   ```bash
   ros2 topic hz /camera/depth/image_raw     # 或 /camera/depth/points
   ros2 topic echo --once /camera/depth/camera_info
   ros2 run tf2_ros tf2_echo camera_link camera_depth_optical_frame
   ```

2. 启动 `duck_audio`，启用特雷门：

   ```bash
   ros2 service call /duck_audio/theremin/enable std_srvs/srv/SetBool "{data: true}"
   ros2 topic echo /duck_audio/theremin/state
   ```

3. 把手放在 15/30/50/70 cm：
   - `range_m` 应接近实际；
   - `closeness` 在 near 处接近 1，far 处接近 0；
   - `note_hz` 随距离单调下降；
   - `mouth` 与 `closeness` 同步。

4. 左右移动 ±20 cm：`note_hz` 不应大幅变化（我们只用 `z`）；如果变化明显，检查是否误用了 `sqrt(x²+y²+z²)` 或 TF 轴向错误。

5. 手贴近相机/机身：不应出现“自己的嘴”持续发声；调 `near_m`、`y_min/y_max`、`lateral_m`。

6. 对着墙：墙在范围内会持续响，这是显式模式的预期行为；如果墙太近导致手无法演奏，调 `far_m` 或改变站位，不要加背景扣除。

7. 遮挡相机 100–200 ms：`held=true`，音符不断；遮挡 > `hold_ms`：静音；恢复后重新发声。

8. 延迟：用秒表/视频同时拍手和扬声器，目标 < 100 ms；若超，先查相机 `depth_fps`、DDS QoS、点云解析。

---

## 8. 边界情况

| 情况 | 行为/处理 |
|---|---|
| 深度图全是 0/NaN | 无手；hold 到期后静音 |
| 点云 `point_step < 12` 或字段缺失 | 本帧丢弃；日志限流；不崩 |
| 点数超过上限（例如 200k） | 丢弃/降采样；防止 15 Hz 下 CPU 打满 |
| 反光/透明物体 | 无深度或无规律；不特殊处理，保持显式模式 |
| 阳光/IR 干扰 | 深度孔洞/飞点；`min_points` 和 `hold` 兜底 |
| 多只手 | 最近的、有支撑的簇获胜；与原 ToF 一致 |
| 手在 FOV 边缘 | 可能只覆盖少量像素；低于 `min_points` 时视为无手 |
| 相机被遮挡 | `stale_ms` 后 `sensor_ok=false`，level 0，嘴 0 |
| TF 不可用 | 若 `target_frame` 为空用相机系；否则本帧丢弃并报状态 |
| `depth_registration=true` + 无彩色 | 点云可能为空/语义不对；改 `false` |
| QoS 不匹配 | 收不到数据；publisher 与 subscriber 都设 `sensor_data` |
| 点云话题存在但无数据 | `publishDepthPointCloud` 在没有订阅者时直接返回；先确保订阅已建立再测 |
| 相机时间戳跳变 | staleness 用本地 `steady_clock` 接收时间；TF 用 header stamp，失败则丢帧 |
| 音频流超时 | `audio_io` 200 ms 无包会结束；writer 必须持续发 10 ms 块 |

---

## 9. 与 `duck_audio` 的接口

`ThereminModule` 只依赖 `HandSource`：

```cpp
class HandSource {
public:
    virtual std::optional<HandSample> latest(std::chrono::steady_clock::time_point) = 0;
    virtual bool healthy() const = 0;
    virtual void reset() = 0;
};
```

`GeminiDepthSource`/`GeminiPointCloudSource` 负责 ROS 订阅、QoS、解析、hold；`ThereminModule` 负责：

- 50 Hz 读取 `HandSample`；
- FFI `duck_hz_at` / `duck_stream_set`；
- 10 ms writer 调 `duck_stream_block`；
- 发布 `~/theremin/state`、`~/mouth`；
- enable/disable 与 `AudioSession` 仲裁。

状态字段建议：

```text
active, sensor_ok, input ("depth_image"|"point_cloud")
range_m, closeness, note_hz, mouth
points, held, stale_ms
diagnostic  // "no frames" / "qos mismatch" / "tf missing" 等
```

`~/theremin/enable` 用 `duck_audio_interface/srv/SetTheremin`：`active=true` 时如果 `healthy()==false`，返回 `accepted=false` 和 `reason`；不要接受后静音。

---

## 10. 测试

### 10.1 单元测试（无相机）

给 `GeminiPointCloudSource`/`GeminiDepthSource` 注入合成帧：

| 用例 | 期望 |
|---|---|
| 10 cm 手，30 个点 | `closeness≈1` |
| 70 cm 手 | `closeness≈0` |
| 40 cm 手 + 60 cm 墙 | 取 40 cm 手，不取墙 |
| 只有 2 个飞点 | 无手（低于 min_points） |
| 空帧/全 0 | hold 内返回 held；超过 hold 为 null |
| 手在 near 外/远外 | 无手，不 clamp 成音符 |
| 输入 stale | `healthy=false`，无手 |
| NaN/Inf | 忽略，不参与直方图 |

### 10.2 bag 回放

录一段 Gemini 深度（15 Hz，手接近/停留/离开/遮挡），用 `ros2 bag play` 驱动 `duck_audio`；检查：

- `theremin/state` 的 `range_m` 与视频一致；
- 音符连续，无“砂砾”断音；
- 手离开后 250 ms 内淡出；
- 遮挡后 `sensor_ok=false`。

### 10.3 硬件

- 距离 10/20/30/50/70 cm 的 `note_hz` 单调；
- 左右 ±20 cm 音高稳定；
- 对着墙的行为符合预期；
- 相机/头部运动时无明显延迟或跳音；
- 连续 10 分钟演奏，CPU/内存稳定，无 DDS 堆积。

---

## 11. 已知风险

1. **深度图/点云没有 ToF 的状态字节**：`min_points`+`hold` 是替代，但不是等价信息；在强阳光/反光场景会更差。
2. **背景不是手**：显式模式下最近的表面会发声；对着墙会响，这是设计，不是 bug。若需要“只认手”，需要额外的分割/分类，超出本功能范围。
3. **相机与嘴不同轴**：直接用 `z` 只在相机光轴与“前方”一致时成立；否则需要 TF 或安装校准。
4. **点云 CPU 成本**：640×480 全量点云 15 Hz 在 ARM 上可能吃掉一个核；优先深度图，或降分辨率/抽稀。
5. **`SLOT_MEAN_S` 与 BLE 无关**：特雷门不使用合唱的 beat；不要把这个常量带进特雷门延迟预算。
6. **`audio_io` 的 stream 是混音而非独占**：`duck_audio` 用 `AudioSession` 做语义独占；不要依赖 `audio_io` 停止单条流（它没有 `stream_id` 停止接口）。
