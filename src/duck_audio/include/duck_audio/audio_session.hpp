#pragma once

#include <mutex>

namespace duck_audio {

    /// 谁拥有当前的实时声音通道。一次性音效只在 None 时播放；wheee 与特雷门互斥。
    enum class AudioOwner {
        None, ///< 空闲：允许一次性 AudioClip。
        Wheee, ///< wheee start/loop/end 流。
        Theremin, ///< 实时特雷门流。
    };

    /// 单一直播通道的租约。所有方法内部加锁，可以被 ROS 回调与 writer 线程同时查询。
    class AudioSession {
    public:
        /// 尝试取得所有权；同一 owner 重复取得是幂等的。
        [[nodiscard]] bool try_acquire(AudioOwner owner);

        /// 释放所有权；只有当前 owner 匹配时才清空。
        void release(AudioOwner owner);

        /// 当前 owner；用于状态发布和 VoiceModule 的 Idle 判断。
        [[nodiscard]] AudioOwner owner() const;

    private:
        mutable std::mutex mutex_;
        AudioOwner owner_{AudioOwner::None}; ///< 受 mutex_ 保护。
    };

} // namespace duck_audio
