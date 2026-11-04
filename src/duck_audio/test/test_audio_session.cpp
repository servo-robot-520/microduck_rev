#include <gtest/gtest.h>

#include "duck_audio/audio_session.hpp"

TEST(AudioSession, EnforcesExclusiveOwnership) {
    duck_audio::AudioSession session;
    EXPECT_EQ(session.owner(), duck_audio::AudioOwner::None);
    EXPECT_FALSE(session.try_acquire(duck_audio::AudioOwner::None));
    EXPECT_TRUE(session.try_acquire(duck_audio::AudioOwner::Wheee));
    EXPECT_TRUE(session.try_acquire(duck_audio::AudioOwner::Wheee));
    EXPECT_FALSE(session.try_acquire(duck_audio::AudioOwner::Theremin));
    EXPECT_EQ(session.owner(), duck_audio::AudioOwner::Wheee);
    session.release(duck_audio::AudioOwner::Theremin);
    EXPECT_EQ(session.owner(), duck_audio::AudioOwner::Wheee);
    session.release(duck_audio::AudioOwner::Wheee);
    EXPECT_EQ(session.owner(), duck_audio::AudioOwner::None);
    EXPECT_TRUE(session.try_acquire(duck_audio::AudioOwner::Theremin));
}
