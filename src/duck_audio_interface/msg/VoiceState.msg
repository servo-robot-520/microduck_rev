# duck_audio 的音库与播放状态快照。
# 建议用 transient_local QoS 发布，新客户端可以立即拿到最新状态。

std_msgs/Header header

# 是否有可用的音库和 seed。false 时 PlayVoice 会明确拒绝，而不是接受后静音。
bool has_voice

# 本机声音种子；来自音库 .seed、硬件 serial 或本地持久值。
uint32 seed

# 音库目录，例如 /var/lib/robot/sounds。
string bank

# 音库版本；与 duck_audio 内置的 BANK_VERSION 不一致时视为过期。
uint32 bank_version

# idle、playing、wheee、theremin、chorale 或 error。
string state

# 最近一次语义 tag；没有播放过时为空。
string last_tag

# 最近一次拒绝/失败原因；正常时为空。
string last_error
