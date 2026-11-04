"""duck_audio_interface 的 ROS endpoint 名称辅助函数。"""

from __future__ import annotations

DEFAULT_NODE_FQN = "/duck_audio"

# 下列私有名称仅供 endpoint owner（duck_audio 节点自身）创建 endpoint 时使用。
PRIVATE_SERVICE_PLAY_VOICE = "~/voice/play"
PRIVATE_TOPIC_WHEEE_HOLD = "~/voice/wheee_hold"
PRIVATE_TOPIC_VOICE_STATE = "~/voice/state"
PRIVATE_SERVICE_SET_THEREMIN = "~/theremin/enable"
PRIVATE_TOPIC_THEREMIN_STATE = "~/theremin/state"
PRIVATE_SERVICE_SET_CHORALE = "~/chorale/enable"
PRIVATE_TOPIC_CHORALE_STATE = "~/chorale/state"
PRIVATE_TOPIC_MOUTH = "~/mouth"
PRIVATE_TOPIC_PETTING_EVENT = "~/petting/event"

_ASCII_WHITESPACE = " \t\n\r\v\f"


def _normalized_node_fqn(node_fqn: str) -> str:
    """校验并返回去掉尾部 '/' 的 duck_audio 节点全名。"""
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


def _endpoint_name(node_fqn: str, endpoint_suffix: str) -> str:
    """拼接节点全名与 endpoint 后缀。"""
    return f"{_normalized_node_fqn(node_fqn)}/{endpoint_suffix}"


def service_play_voice(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回一次性语义声音服务的绝对名称。"""
    return _endpoint_name(node_fqn, "voice/play")


def service_set_theremin(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回特雷门启停服务的绝对名称。"""
    return _endpoint_name(node_fqn, "theremin/enable")


def service_set_chorale(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回合唱启停服务的绝对名称。"""
    return _endpoint_name(node_fqn, "chorale/enable")


def topic_wheee_hold(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回 wheee 按住信号话题的绝对名称。"""
    return _endpoint_name(node_fqn, "voice/wheee_hold")


def topic_voice_state(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回声音状态话题的绝对名称。"""
    return _endpoint_name(node_fqn, "voice/state")


def topic_theremin_state(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回特雷门状态话题的绝对名称。"""
    return _endpoint_name(node_fqn, "theremin/state")


def topic_chorale_state(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回合唱状态话题的绝对名称。"""
    return _endpoint_name(node_fqn, "chorale/state")


def topic_mouth(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回嘴部目标话题的绝对名称。"""
    return _endpoint_name(node_fqn, "mouth")


def topic_petting_event(node_fqn: str = DEFAULT_NODE_FQN) -> str:
    """返回摸头事件的绝对名称。"""
    return _endpoint_name(node_fqn, "petting/event")


__all__ = [
    "DEFAULT_NODE_FQN",
    "PRIVATE_SERVICE_PLAY_VOICE",
    "PRIVATE_TOPIC_WHEEE_HOLD",
    "PRIVATE_TOPIC_VOICE_STATE",
    "PRIVATE_SERVICE_SET_THEREMIN",
    "PRIVATE_TOPIC_THEREMIN_STATE",
    "PRIVATE_SERVICE_SET_CHORALE",
    "PRIVATE_TOPIC_CHORALE_STATE",
    "PRIVATE_TOPIC_MOUTH",
    "service_play_voice",
    "service_set_theremin",
    "service_set_chorale",
    "topic_wheee_hold",
    "topic_voice_state",
    "topic_theremin_state",
    "topic_chorale_state",
    "topic_mouth",
]
