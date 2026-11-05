#include <stdexcept>
#include <type_traits>

#include <gtest/gtest.h>

#include "duck_audio_interface/ros_msg.hpp"

namespace {

constexpr std::string_view kRobotNodeFqn = "/robot_a/duck_audio";

TEST(RosMsgHelpers, ProvidesPrivateEndpointOwnerNames) {
    EXPECT_EQ(duck_audio_interface::kPrivateServicePlayVoice, "~/voice/play");
    EXPECT_EQ(duck_audio_interface::kPrivateTopicWheeeHold, "~/voice/wheee_hold");
    EXPECT_EQ(duck_audio_interface::kPrivateTopicVoiceState, "~/voice/state");
    EXPECT_EQ(duck_audio_interface::kPrivateServiceSetTheremin, "~/theremin/enable");
    EXPECT_EQ(duck_audio_interface::kPrivateTopicThereminState, "~/theremin/state");
    EXPECT_EQ(duck_audio_interface::kPrivateServiceSetChorale, "~/chorale/enable");
    EXPECT_EQ(duck_audio_interface::kPrivateTopicChoraleState, "~/chorale/state");
    EXPECT_EQ(duck_audio_interface::kPrivateTopicMouth, "~/mouth");
}

TEST(RosMsgHelpers, BuildsDefaultAbsoluteEndpointNames) {
    EXPECT_EQ(duck_audio_interface::service_play_voice(), "/duck_audio/voice/play");
    EXPECT_EQ(duck_audio_interface::topic_wheee_hold(), "/duck_audio/voice/wheee_hold");
    EXPECT_EQ(duck_audio_interface::topic_voice_state(), "/duck_audio/voice/state");
    EXPECT_EQ(duck_audio_interface::service_set_theremin(), "/duck_audio/theremin/enable");
    EXPECT_EQ(duck_audio_interface::topic_theremin_state(), "/duck_audio/theremin/state");
    EXPECT_EQ(duck_audio_interface::service_set_chorale(), "/duck_audio/chorale/enable");
    EXPECT_EQ(duck_audio_interface::topic_chorale_state(), "/duck_audio/chorale/state");
    EXPECT_EQ(duck_audio_interface::topic_mouth(), "/duck_audio/mouth");
}

TEST(RosMsgHelpers, QualifiesDefaultNodeWithNamespace) {
    EXPECT_EQ(duck_audio_interface::qualify_node_fqn(), "/duck_audio");
    EXPECT_EQ(duck_audio_interface::qualify_node_fqn(
                      duck_audio_interface::kDefaultNodeFqn, "/robot_a"),
              "/robot_a/duck_audio");
    EXPECT_EQ(duck_audio_interface::service_play_voice(
                      duck_audio_interface::kDefaultNodeFqn, "/robot_a"),
              "/robot_a/duck_audio/voice/play");
    EXPECT_EQ(duck_audio_interface::topic_petting_event(
                      duck_audio_interface::kDefaultNodeFqn, "/robot_a"),
              "/robot_a/duck_audio/petting/event");
}

TEST(RosMsgHelpers, BuildsNamesForCustomNodeFqn) {
    EXPECT_EQ(duck_audio_interface::service_play_voice(kRobotNodeFqn),
              "/robot_a/duck_audio/voice/play");
    EXPECT_EQ(duck_audio_interface::service_set_theremin(kRobotNodeFqn),
              "/robot_a/duck_audio/theremin/enable");
    EXPECT_EQ(duck_audio_interface::topic_voice_state(kRobotNodeFqn),
              "/robot_a/duck_audio/voice/state");
    EXPECT_EQ(duck_audio_interface::topic_mouth(std::string{kRobotNodeFqn} + "///"),
              "/robot_a/duck_audio/mouth");
}

TEST(RosMsgHelpers, RejectsInvalidNodeFqn) {
    EXPECT_THROW(static_cast<void>(duck_audio_interface::service_play_voice("")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(duck_audio_interface::service_play_voice("duck/audio")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(duck_audio_interface::service_play_voice("/")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(duck_audio_interface::service_play_voice("/duck//audio")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(duck_audio_interface::service_play_voice("/duck_audio\t")),
                 std::invalid_argument);
}

TEST(RosMsgHelpers, ExposesGeneratedTypeAliases) {
    static_assert(std::is_same_v<duck_audio_interface::MsgVoiceState,
                                 duck_audio_interface::msg::VoiceState>);
    static_assert(std::is_same_v<duck_audio_interface::MsgThereminState,
                                 duck_audio_interface::msg::ThereminState>);
    static_assert(std::is_same_v<duck_audio_interface::MsgChoraleState,
                                 duck_audio_interface::msg::ChoraleState>);
    static_assert(std::is_same_v<duck_audio_interface::MsgWheeeHold,
                                 duck_audio_interface::msg::WheeeHold>);
    static_assert(std::is_same_v<duck_audio_interface::SrvPlayVoice,
                                 duck_audio_interface::srv::PlayVoice>);
    static_assert(std::is_same_v<duck_audio_interface::SrvSetTheremin,
                                 duck_audio_interface::srv::SetTheremin>);
    static_assert(std::is_same_v<duck_audio_interface::SrvSetChorale,
                                 duck_audio_interface::srv::SetChorale>);
}

} // namespace
