# wheee 滑行的电平信号。
# 客户端按住时以 5–10 Hz 持续发布 held=true；释放时发布 held=false。
# duck_audio 在超过 300 ms 没有新消息时按“松手落地”处理，避免客户端崩溃后无限循环。

std_msgs/Header header
bool held
