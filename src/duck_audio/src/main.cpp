#include <exception>
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "duck_audio/duck_audio_node.hpp"

int main(const int argc, char** argv) {
    rclcpp::init(argc, argv);
    const auto node = std::make_shared<rclcpp::Node>("duck_audio");

    try {
        const auto audio = std::make_shared<duck_audio::DuckAudioNode>(node);
        rclcpp::executors::MultiThreadedExecutor executor;
        executor.add_node(node);
        executor.spin();
        executor.remove_node(node);
    } catch (const std::exception& exception) {
        RCLCPP_FATAL(node->get_logger(), "duck_audio terminated: %s", exception.what());
        rclcpp::shutdown();
        return 1;
    } catch (...) {
        RCLCPP_FATAL(node->get_logger(), "duck_audio terminated with an unknown error");
        rclcpp::shutdown();
        return 1;
    }

    rclcpp::shutdown();
    return 0;
}
