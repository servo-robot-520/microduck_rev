# 鸭子合唱的四声部状态。

# 声部编号，低到高；与 sounds::chorale::Part 一致。
uint8 PART_BASS=0
uint8 PART_TENOR=1
uint8 PART_ALTO=2
uint8 PART_SOPRANO=3

std_msgs/Header header

# 是否正在参与合唱。
bool active

# off、listening、conducting、following 或 error。
string state

# 当前曲目名；未开始时为空。
string piece

# 本鸭声部，取 PART_*。
uint8 part

# 当前 score 位置，单位 beat。
float32 position_beats

# 实际在唱的鸭子数（不包含已经离开但保留座位的）。
uint8 voices

# 正在等拍/等 roster，还没开口。
bool joining
