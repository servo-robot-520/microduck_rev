#pragma once

#include <mutex>

namespace duck_audio {

/// 谁拥有当前的实时声音通道。一次性音效只在 None 时播放；wheee 与特雷门互斥。
enum class AudioOwner {
    None,
    Wheee,
    Theremin,
};

class AudioSession {
public:
    /// 尝试取得所有权；同一 owner 重复取得是幂等的。
    [[nodiscard]] bool try_acquire(AudioOwner owner);

    /// 释放所有权；只有当前 owner 匹配时才清空。
    void release(AudioOwner owner);

    [[nodiscard]] AudioOwner owner() const;

private:
    mutable std::mutex mutex_;
    AudioOwner owner_{AudioOwner::None};
};

} // namespace duck_audio
