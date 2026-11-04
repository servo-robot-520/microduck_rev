if (NOT ROS2_WS_SOURCE_BUILD OR NOT TARGET duck_audio_interface)
    find_package(duck_audio_interface REQUIRED)
endif ()

if (ROS2_WS_SOURCE_BUILD AND TARGET duck_audio_interface)
    # 在源码聚合构建中，依赖包尚未安装到 install/，因此直接使用构建树中的
    # typesupport target 与源码 include 目录。
    rosidl_get_typesupport_target(duck_audio_interface_CPP_TYPESUPPORT_TARGET
            duck_audio_interface
            "rosidl_typesupport_cpp"
    )
    rosidl_get_typesupport_target(duck_audio_interface_FASTRTPS_CPP_TYPESUPPORT_TARGET
            duck_audio_interface
            "rosidl_typesupport_fastrtps_cpp"
    )
    rosidl_get_typesupport_target(duck_audio_interface_INTROSPECTION_CPP_TYPESUPPORT_TARGET
            duck_audio_interface
            "rosidl_typesupport_introspection_cpp"
    )
    if (duck_audio_interface_CPP_TYPESUPPORT_TARGET MATCHES "-NOTFOUND$"
            OR duck_audio_interface_FASTRTPS_CPP_TYPESUPPORT_TARGET MATCHES "-NOTFOUND$"
            OR duck_audio_interface_INTROSPECTION_CPP_TYPESUPPORT_TARGET MATCHES "-NOTFOUND$")
        message(FATAL_ERROR "The in-tree duck_audio_interface target does not provide rosidl typesupport targets.")
    endif ()
    if (NOT DEFINED duck_audio_interface_SOURCE_DIR
            OR NOT IS_DIRECTORY "${duck_audio_interface_SOURCE_DIR}/include")
        message(FATAL_ERROR "The in-tree duck_audio_interface source include directory is unavailable.")
    endif ()
    target_link_libraries(${PROJECT_NAME} PUBLIC
            ${duck_audio_interface_CPP_TYPESUPPORT_TARGET}
    )
    target_include_directories(${PROJECT_NAME} PUBLIC
            "$<BUILD_INTERFACE:${duck_audio_interface_SOURCE_DIR}/include>")
else ()
    ament_target_dependencies(${PROJECT_NAME} duck_audio_interface)
endif ()

if (ROS2_WS_SOURCE_BUILD)
    target_link_libraries(${PROJECT_NAME} PUBLIC
            "-Wl,--no-as-needed"
            ${duck_audio_interface_FASTRTPS_CPP_TYPESUPPORT_TARGET}
            ${duck_audio_interface_INTROSPECTION_CPP_TYPESUPPORT_TARGET}
            "-Wl,--as-needed"
    )
endif ()
