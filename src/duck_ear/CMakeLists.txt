cmake_minimum_required(VERSION 3.22)
project(duck_ear VERSION 0.1.0)

option(BUILD_EXE "Build node executable file" ON)
option(BUILD_TESTING "Build unit tests" ON)
option(DUCK_EAR_BUILD_RUST "Build the pet-detect static library" ON)
option(DUCK_EAR_USE_PREBUILT_RUST "Use a prebuilt libduck_ear_ffi.a" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(std_msgs REQUIRED)

if (NOT ROS2_WS_SOURCE_BUILD)
    find_package(common_cpp REQUIRED)
endif ()

# ---------------------------------------------------------------------------
# Rust pet-detect: static lib, linked into libduck_ear.so
# ---------------------------------------------------------------------------
set(DUCK_EAR_RUST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust")
set(DUCK_EAR_RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
set(DUCK_EAR_RUST_LIB "${DUCK_EAR_RUST_TARGET_DIR}/release/libduck_ear_ffi.a")
if (CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_TARGET)
    set(DUCK_EAR_RUST_LIB
            "${DUCK_EAR_RUST_TARGET_DIR}/${CMAKE_CXX_COMPILER_TARGET}/release/libduck_ear_ffi.a")
endif ()

if (DUCK_EAR_USE_PREBUILT_RUST)
    find_library(DUCK_EAR_RUST_LIBRARY NAMES duck_ear_ffi REQUIRED)
    set(DUCK_EAR_RUST_LIB "${DUCK_EAR_RUST_LIBRARY}")
elseif (DUCK_EAR_BUILD_RUST)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)
    file(GLOB_RECURSE DUCK_EAR_RUST_SOURCES CONFIGURE_DEPENDS
            "${DUCK_EAR_RUST_DIR}/pet-detect/src/*.rs"
            "${DUCK_EAR_RUST_DIR}/duck_ear_ffi/src/*.rs")
    set(DUCK_EAR_CARGO_ARGS build --release --locked --manifest-path
            "${DUCK_EAR_RUST_DIR}/Cargo.toml" -p duck_ear_ffi)
    if (CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_TARGET)
        list(APPEND DUCK_EAR_CARGO_ARGS --target "${CMAKE_CXX_COMPILER_TARGET}")
    endif ()
    add_custom_command(
            OUTPUT "${DUCK_EAR_RUST_LIB}"
            COMMAND ${CMAKE_COMMAND} -E env
            "CARGO_TARGET_DIR=${DUCK_EAR_RUST_TARGET_DIR}"
            "MAKEFLAGS="
            ${CARGO_EXECUTABLE} ${DUCK_EAR_CARGO_ARGS}
            WORKING_DIRECTORY "${DUCK_EAR_RUST_DIR}"
            DEPENDS ${DUCK_EAR_RUST_SOURCES}
            "${DUCK_EAR_RUST_DIR}/Cargo.toml"
            "${DUCK_EAR_RUST_DIR}/Cargo.lock"
            COMMENT "Building libduck_ear_ffi.a"
            VERBATIM)
    add_custom_target(duck_ear_rust ALL DEPENDS "${DUCK_EAR_RUST_LIB}")
endif ()

# ---------------------------------------------------------------------------
# Core library: libduck_ear.so
# ---------------------------------------------------------------------------
add_library(duck_ear SHARED
        include/duck_ear/duck_ear_node.hpp
        include/duck_ear/ear_processor.hpp
        include/duck_ear/linear_resampler.hpp
        include/duck_ear/ffi/duck_ear_ffi.h
        src/duck_ear_node.cpp
        src/ear_processor.cpp
        src/linear_resampler.cpp
)
target_compile_features(duck_ear PUBLIC cxx_std_23)
set_target_properties(duck_ear PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(duck_ear PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if (ROS2_WS_SOURCE_BUILD)
    if (NOT TARGET audio_io_interface OR NOT TARGET duck_audio_interface)
        message(FATAL_ERROR "CLion workspace requires in-tree audio/duck audio interfaces.")
    endif ()
    target_include_directories(duck_ear PUBLIC
            ${rclcpp_INCLUDE_DIRS}
            ${std_msgs_INCLUDE_DIRS}
            ${ament_index_cpp_INCLUDE_DIRS})
    target_link_libraries(duck_ear PUBLIC
            ${rclcpp_LIBRARIES}
            ${std_msgs_LIBRARIES}
            ${ament_index_cpp_LIBRARIES})
    include("${audio_io_interface_SOURCE_DIR}/cmake_include.cmake")
    include("${duck_audio_interface_SOURCE_DIR}/cmake_include.cmake")
else ()
    find_package(audio_io_interface REQUIRED)
    find_package(duck_audio_interface REQUIRED)
    ament_target_dependencies(duck_ear PUBLIC
            rclcpp
            std_msgs
            ament_index_cpp
            audio_io_interface
            duck_audio_interface)
endif ()

if (DUCK_EAR_BUILD_RUST)
    add_dependencies(duck_ear duck_ear_rust)
endif ()
target_link_libraries(duck_ear PRIVATE "${DUCK_EAR_RUST_LIB}" dl pthread m)
set_target_properties(duck_ear PROPERTIES INSTALL_RPATH "$ORIGIN")

if (BUILD_EXE)
    add_executable(duck_ear_node src/main.cpp)
    target_compile_features(duck_ear_node PRIVATE cxx_std_23)
    target_link_libraries(duck_ear_node duck_ear)
    if (ROS2_WS_SOURCE_BUILD)
        if (NOT TARGET common_cpp_ros_logging)
            message(FATAL_ERROR
                    "CLion workspace configuration requires the in-tree common_cpp_ros_logging target.")
        endif ()
        target_link_libraries(duck_ear_node common_cpp_ros_logging)
    else ()
        target_link_libraries(duck_ear_node common_cpp::common_cpp_ros_logging)
    endif ()
    set_target_properties(duck_ear_node PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif ()

if (BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)
    ament_add_gtest(test_linear_resampler test/test_linear_resampler.cpp)
    target_link_libraries(test_linear_resampler duck_ear)
endif ()

install(TARGETS duck_ear
        EXPORT duck_earTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include)
if (BUILD_EXE)
    install(TARGETS duck_ear_node DESTINATION lib/${PROJECT_NAME})
    install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})
endif ()
install(DIRECTORY models DESTINATION share/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
ament_export_include_directories(include)
ament_export_dependencies(rclcpp std_msgs ament_index_cpp audio_io_interface duck_audio_interface common_cpp)
ament_export_targets(duck_earTargets HAS_LIBRARY_TARGET)
ament_package()
