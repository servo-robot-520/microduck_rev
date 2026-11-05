#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "duck_asr_interface/ros_msg.hpp"

namespace {

    constexpr std::string_view kRobotNodeFqn = "/robot_a/duck_asr";

    TEST(RosMsgHelpers, ProvidesPrivateEndpointOwnerNames) {
        EXPECT_EQ(duck_asr_interface::kPrivateTopicIntent, "~/intent");
    }

    TEST(RosMsgHelpers, BuildsDefaultAbsoluteEndpointNames) {
        EXPECT_EQ(duck_asr_interface::topic_intent(), "/duck_asr/intent");
    }

    TEST(RosMsgHelpers, QualifiesDefaultNodeWithNamespace) {
        EXPECT_EQ(duck_asr_interface::qualify_node_fqn("/"), "/duck_asr");
        EXPECT_EQ(duck_asr_interface::qualify_node_fqn("/robot_a"), "/robot_a/duck_asr");
        EXPECT_EQ(duck_asr_interface::topic_intent(duck_asr_interface::kDefaultNodeFqn, "/robot_a"),
                  "/robot_a/duck_asr/intent");
    }

    TEST(RosMsgHelpers, BuildsNamesForCustomNodeFqn) {
        EXPECT_EQ(duck_asr_interface::topic_intent(kRobotNodeFqn), "/robot_a/duck_asr/intent");
        EXPECT_EQ(duck_asr_interface::topic_intent(std::string{kRobotNodeFqn} + "///"),
                  "/robot_a/duck_asr/intent");
    }

    TEST(RosMsgHelpers, RejectsInvalidNodeFqn) {
        EXPECT_THROW(static_cast<void>(duck_asr_interface::topic_intent("")), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(duck_asr_interface::topic_intent("duck_asr")), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(duck_asr_interface::topic_intent("/")), std::invalid_argument);
        EXPECT_THROW(static_cast<void>(duck_asr_interface::topic_intent("/duck//asr")),
                     std::invalid_argument);
        EXPECT_THROW(static_cast<void>(duck_asr_interface::topic_intent("/duck_asr\t")),
                     std::invalid_argument);
    }

    TEST(RosMsgHelpers, ExposesGeneratedTypeAliases) {
        static_assert(std::is_same_v<duck_asr_interface::MsgDuckIntent,
                                     duck_asr_interface::msg::DuckIntent>);
    }

} // namespace
