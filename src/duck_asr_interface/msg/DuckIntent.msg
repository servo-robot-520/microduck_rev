# 鸭子口令意图。由 duck_asr 发布；具体动作由行为/控制节点执行。

# 时间戳与 frame_id。
std_msgs/Header header

# 对应的 asr 会话标识，便于把意图和识别结果串起来。
string session_id

# 意图名称，例如 battery.query、posture.stand、action.nod。
# 使用字符串而不是枚举，避免新增口令就要修改接口包。
string intent

# 触发该意图的原始识别文本。
string utterance

# 意图匹配的置信度；无置信度时为 0。
float32 confidence

# 意图参数键；与 arg_values 一一对应。
string[] arg_keys

# 意图参数值；与 arg_keys 一一对应。
string[] arg_values
