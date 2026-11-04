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

install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION include/${PROJECT_NAME}/${PROJECT_NAME}
)
ament_export_include_directories(include/${PROJECT_NAME})

install(DIRECTORY python/${PROJECT_NAME}_msg/
        DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME}_msg
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
)

if (BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)
    find_package(ament_cmake_pytest REQUIRED)
    ament_add_gtest(test_ros_msg_helpers test/test_ros_msg_helpers.cpp)
    target_include_directories(test_ros_msg_helpers PRIVATE include)
    target_link_libraries(test_ros_msg_helpers
            ${PROJECT_NAME}__rosidl_typesupport_cpp
    )
    ament_add_pytest_test(test_interface_contract test/test_interface_contract.py)
endif ()

ament_export_dependencies(rosidl_default_runtime)
ament_package()
