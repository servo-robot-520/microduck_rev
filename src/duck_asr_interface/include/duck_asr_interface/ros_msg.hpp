#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "duck_asr_interface/msg/duck_intent.hpp"

namespace duck_asr_interface {

    namespace detail {

        /// 默认 duck_asr 节点全名。
        inline constexpr std::string_view kDefaultNodeFqn = "/duck_asr";

        /// 去掉尾部多余的 '/'，保留开头的绝对路径。
        [[nodiscard]] inline std::string_view trim_trailing_slashes(std::string_view node_fqn) {
            while (node_fqn.size() > 1 && node_fqn.back() == '/') {
                node_fqn.remove_suffix(1);
            }
            return node_fqn;
        }

        /// 校验节点全名：必须是非根绝对名称，不能包含 ASCII 空白或中间的空路径段。
        inline void require_node_fqn(const std::string_view raw_node_fqn) {
            if (const auto node_fqn = trim_trailing_slashes(raw_node_fqn);
                node_fqn.empty() || node_fqn.front() != '/' || node_fqn == "/" ||
                node_fqn.find("//") != std::string_view::npos ||
                node_fqn.find_first_of(" \t\n\r\v\f") != std::string_view::npos) {
                throw std::invalid_argument(
                    "node_fqn must be an absolute, non-root ROS name without ASCII whitespace");
            }
        }

        /// 校验并返回去掉尾部 '/' 的节点全名。
        [[nodiscard]] inline std::string normalized_node_fqn(const std::string_view node_fqn) {
            require_node_fqn(node_fqn);
            return std::string{trim_trailing_slashes(node_fqn)};
        }

        /// 拼接节点全名与 endpoint 后缀。
        [[nodiscard]] inline std::string endpoint_name(const std::string_view node_fqn,
                                                       const std::string_view endpoint_suffix) {
            return normalized_node_fqn(node_fqn) + "/" + std::string{endpoint_suffix};
        }

    } // namespace detail

    /// 默认 duck_asr 节点全名。
    inline constexpr std::string_view kDefaultNodeFqn = detail::kDefaultNodeFqn;

    /// 把默认节点全名限定到调用方 namespace；根 namespace 保持默认名。
    [[nodiscard]] inline std::string qualify_node_fqn(
            const std::string_view node_namespace,
            const std::string_view default_node_fqn = detail::kDefaultNodeFqn) {
        const auto base = detail::trim_trailing_slashes(default_node_fqn);
        detail::require_node_fqn(base);
        auto ns = detail::trim_trailing_slashes(node_namespace);
        if (ns.empty()) {
            ns = "/";
        }
        if (ns != "/") {
            detail::require_node_fqn(ns);
        }
        if (ns == "/") {
            return std::string{base};
        }
        return detail::normalized_node_fqn(std::string{ns} + std::string{base});
    }

    /// endpoint owner（duck_asr 节点自身）创建 publisher 时使用的私有名称。
    inline constexpr std::string_view kPrivateTopicIntent = "~/intent";

    /// 外部消费者使用的绝对话题名称。
    /// @param node_fqn duck_asr 在根 namespace 下的节点全名，默认 /duck_asr
    /// @param node_namespace 调用方 namespace；默认 "/"
    [[nodiscard]] inline std::string topic_intent(
            const std::string_view node_fqn = detail::kDefaultNodeFqn,
            const std::string_view node_namespace = "/") {
        return detail::endpoint_name(qualify_node_fqn(node_namespace, node_fqn), "intent");
    }

    // 与生成消息名称一致的简洁类型别名。
    using MsgDuckIntent = duck_asr_interface::msg::DuckIntent;

} // namespace duck_asr_interface
