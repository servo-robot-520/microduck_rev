# 摸头事件。由 duck_ear 发布；duck_audio 或行为/控制节点消费。
# 只描述“开始/结束”和置信度，不规定具体动作。

uint8 START=0
uint8 END=1

std_msgs/Header header

# 取 START/END。
uint8 event

# 分类器对本次判决的置信度；无模型或未知时为 0。
float32 confidence
