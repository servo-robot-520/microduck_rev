cmake_minimum_required(VERSION 3.22)
project(duck_bringup)

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

find_package(ament_cmake REQUIRED)


install(DIRECTORY
        launch
        DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY scripts
        DESTINATION share/${PROJECT_NAME}
        USE_SOURCE_PERMISSIONS
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config")
    install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
endif ()

ament_package()
