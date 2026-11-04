#include "duck_audio/audio_session.hpp"

namespace duck_audio {

// None 不是可取得的所有权；重复取得同一 owner 用于 lease 刷新。
bool AudioSession::try_acquire(const AudioOwner owner) {
    if (owner == AudioOwner::None) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (owner_ == AudioOwner::None || owner_ == owner) {
        owner_ = owner;
        return true;
    }
    return false;
}

// 只释放自己持有的 owner，避免迟到的旧流把新流的所有权清掉。
void AudioSession::release(const AudioOwner owner) {
    std::scoped_lock lock(mutex_);
    if (owner_ == owner) {
        owner_ = AudioOwner::None;
    }
}

AudioOwner AudioSession::owner() const {
    std::scoped_lock lock(mutex_);
    return owner_;
}

} // namespace duck_audio
