#include "duck_audio/audio_session.hpp"

namespace duck_audio {

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
