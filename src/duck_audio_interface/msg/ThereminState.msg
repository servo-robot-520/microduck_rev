# 特雷门状态。传感器可以是 ToF，也可以是 Gemini 深度图/点云。

std_msgs/Header header

# 乐器是否被启用（服务成功返回后为 true）。
bool active

# 输入是否健康：最近一帧未超时，且 QoS/TF 正常。
bool sensor_ok

# depth_image、point_cloud 或 tof。
string input

# 手到传感器的距离，米；无手时为 0。
float32 range_m

# 0 远、1 近；无手时为 0。
float32 closeness

# 当前音高，Hz；无手时保持最后一次有效音高。
float32 note_hz

# 嘴部目标，0..1；与 note_hz 使用同一个 closeness。
float32 mouth

# 当前深度簇的支撑点数/像素数。
uint32 points

# true 表示这是 hold 的上一帧，而不是本帧新测量。
bool held

# 诊断信息，例如 no frames、qos mismatch、tf missing。
string diagnostic
