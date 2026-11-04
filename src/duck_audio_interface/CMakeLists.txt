cmake_minimum_required(VERSION 3.22)
project(duck_audio_interface VERSION 0.1.0)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
        "msg/VoiceState.msg"
        "msg/ThereminState.msg"
        "msg/ChoraleState.msg"
        "msg/WheeeHold.msg"
        "srv/PlayVoice.srv"
        "srv/SetTheremin.srv"
        "srv/SetChorale.srv"
        DEPENDENCIES
        builtin_interfaces
        std_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
