"""duck_asr_interface 的 ROS endpoint 名称辅助函数。"""

from __future__ import annotations

DEFAULT_NODE_FQN = "/duck_asr"

# 下列私有名称仅供 endpoint owner（duck_asr 节点自身）创建 endpoint 时使用。
PRIVATE_TOPIC_INTENT = "~/intent"

_ASCII_WHITESPACE = " \t\n\r\v\f"


def _normalized_node_fqn(node_fqn: str) -> str:
    """校验并返回去掉尾部 '/' 的 duck_asr 节点全名。"""
    trimmed = node_fqn.rstrip("/")
    if (
            not trimmed
            or not trimmed.startswith("/")
            or trimmed == "/"
            or "//" in trimmed
            or any(character in _ASCII_WHITESPACE for character in trimmed)
    ):
        raise ValueError(
            "node_fqn must be an absolute, non-root ROS name without ASCII whitespace"
        )
    return trimmed


def _endpoint_name(node_fqn: str, endpoint_suffix: str,
                   node_namespace: str = "/") -> str:
    """限定节点 FQN 后拼接 endpoint 后缀。"""
    return f"{qualify_node_fqn(node_namespace, node_fqn)}/{endpoint_suffix}"


def qualify_node_fqn(node_namespace: str, default_node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """把默认 duck_asr 节点全名限定到调用方 namespace。"""
    base = _normalized_node_fqn(default_node_fqn)
    namespace = node_namespace.rstrip("/") or "/"
    if namespace != "/":
        namespace = _normalized_node_fqn(namespace)
    if namespace == "/":
        return base
    return _normalized_node_fqn(f"{namespace}{base}")


def topic_intent(node_fqn: str = DEFAULT_NODE_FQN, node_namespace: str = "/") -> str:
    """返回鸭子意图话题的绝对名称。"""
    return _endpoint_name(node_fqn, "intent", node_namespace)


__all__ = [
    "DEFAULT_NODE_FQN",
    "PRIVATE_TOPIC_INTENT",
    "qualify_node_fqn",
    "topic_intent",
]
