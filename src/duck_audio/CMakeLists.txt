cmake_minimum_required(VERSION 3.22)
project(duck_audio VERSION 0.1.0)

option(BUILD_EXE "Build node executable file" ON)
option(BUILD_TESTING "Build unit tests" ON)
option(DUCK_AUDIO_BUILD_RUST "Build libmicroduck_sounds from rust/" ON)
option(DUCK_AUDIO_USE_PREBUILT_RUST "Use an already installed libmicroduck_sounds" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT SKIP_CXX23_STDLIB_CHECK AND NOT _CXX23_STDLIB_VERIFIED)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_QUIET TRUE)
    string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++23")
    string(TOUPPER "${PROJECT_NAME}" _PRJ_UPPER)
    check_cxx_source_compiles([[
        #include <expected>
        #include <format>
        #include <ranges>
        int main() {
            std::expected<int, int> result{42};
            const auto text = std::format("{}", result.value());
            const auto values = std::views::iota(0, 1);
            return static_cast<int>(text.size() + *values.begin());
        }
    ]] ${_PRJ_UPPER}_HAS_REQUIRED_CXX23_STDLIB)
    if (NOT ${_PRJ_UPPER}_HAS_REQUIRED_CXX23_STDLIB)
        message(FATAL_ERROR
                "${PROJECT_NAME} requires a C++23 standard library with <expected>, "
                "<format>, and ranges support. Use GCC 13+ or an equivalent compiler.")
    endif ()
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

if (NOT ROS2_WS_SOURCE_BUILD)
    find_package(common_cpp REQUIRED)
endif ()

# ---------------------------------------------------------------------------
# Rust FFI: libmicroduck_sounds.so
# ---------------------------------------------------------------------------
set(DUCK_RUST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust")
set(DUCK_RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
set(DUCK_RUST_TARGET_TRIPLE "")
set(DUCK_SOUNDS_BIN "${DUCK_RUST_TARGET_DIR}/release/sounds")
if (CMAKE_CROSSCOMPILING)
    set(DUCK_RUST_TARGET_TRIPLE "${CMAKE_CXX_COMPILER_TARGET}")
endif ()
if (DUCK_RUST_TARGET_TRIPLE)
    set(DUCK_RUST_LIB "${DUCK_RUST_TARGET_DIR}/${DUCK_RUST_TARGET_TRIPLE}/release/libmicroduck_sounds.so")
    set(DUCK_SOUNDS_BIN "${DUCK_RUST_TARGET_DIR}/${DUCK_RUST_TARGET_TRIPLE}/release/sounds")
else ()
    set(DUCK_RUST_LIB "${DUCK_RUST_TARGET_DIR}/release/libmicroduck_sounds.so")
endif ()

if (DUCK_AUDIO_USE_PREBUILT_RUST)
    find_library(DUCK_MICRODUCK_SOUNDS_LIBRARY NAMES microduck_sounds REQUIRED)
    set(DUCK_RUST_LIB "${DUCK_MICRODUCK_SOUNDS_LIBRARY}")
elseif (DUCK_AUDIO_BUILD_RUST)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)
    file(GLOB_RECURSE DUCK_RUST_SOURCES CONFIGURE_DEPENDS
            "${DUCK_RUST_DIR}/sounds/src/*.rs"
            "${DUCK_RUST_DIR}/robotd-params/src/*.rs"
            "${DUCK_RUST_DIR}/microduck_sounds_ffi/src/*.rs")
    # 同时构建 FFI 库和原版 sounds CLI：后者就是 ensure-bank 的来源。
    set(DUCK_CARGO_ARGS build --release --locked --manifest-path
            "${DUCK_RUST_DIR}/Cargo.toml" -p microduck_sounds_ffi -p sounds)
    if (DUCK_RUST_TARGET_TRIPLE)
        list(APPEND DUCK_CARGO_ARGS --target "${DUCK_RUST_TARGET_TRIPLE}")
    endif ()
    add_custom_command(
            OUTPUT "${DUCK_RUST_LIB}" "${DUCK_SOUNDS_BIN}"
            COMMAND ${CMAKE_COMMAND} -E env
            "CARGO_TARGET_DIR=${DUCK_RUST_TARGET_DIR}"
            "MAKEFLAGS="
            ${CARGO_EXECUTABLE} ${DUCK_CARGO_ARGS}
            WORKING_DIRECTORY "${DUCK_RUST_DIR}"
            DEPENDS ${DUCK_RUST_SOURCES}
            "${DUCK_RUST_DIR}/Cargo.toml"
            "${DUCK_RUST_DIR}/Cargo.lock"
            "${DUCK_RUST_DIR}/UPSTREAM_MICRODUCK_COMMIT"
            COMMENT "Building libmicroduck_sounds with cargo"
            VERBATIM)
    add_custom_target(duck_audio_rust ALL DEPENDS "${DUCK_RUST_LIB}" "${DUCK_SOUNDS_BIN}")
    add_library(microduck_sounds SHARED IMPORTED)
    set_target_properties(microduck_sounds PROPERTIES
            IMPORTED_LOCATION "${DUCK_RUST_LIB}"
            IMPORTED_NO_SONAME TRUE)
endif ()

# ---------------------------------------------------------------------------
# Core library: libduck_audio
# ---------------------------------------------------------------------------
add_library(duck_audio SHARED
        include/duck_audio/audio_session.hpp
        include/duck_audio/duck_audio_node.hpp
        include/duck_audio/gemini_hand_source.hpp
        include/duck_audio/hand_source.hpp
        include/duck_audio/hand_tracker.hpp
        include/duck_audio/stream_publisher.hpp
        include/duck_audio/theremin_module.hpp
        include/duck_audio/voice_module.hpp
        include/duck_audio/voice_runtime.hpp
        include/duck_audio/wav_pcm_reader.hpp
        include/duck_audio/wheee_player.hpp
        include/duck_audio/ffi/microduck_sounds.h
        src/audio_session.cpp
        src/duck_audio_node.cpp
        src/gemini_hand_source.cpp
        src/hand_tracker.cpp
        src/stream_publisher.cpp
        src/theremin_module.cpp
        src/voice_module.cpp
        src/voice_runtime.cpp
        src/wav_pcm_reader.cpp
        src/wheee_player.cpp
)
target_compile_features(duck_audio PUBLIC cxx_std_23)
set_target_properties(duck_audio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(duck_audio PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
# 接口包的 endpoint/typesupport 统一由各自的 cmake_include.cmake 处理：
# 普通 colcon 构建走 find_package，CLion 源码聚合构建直接用 in-tree target。
# 源码聚合的 helper 使用 keyword target_link_libraries，因此这一分支也统一用 keyword；
# 普通 colcon 的 helper 使用 ament_target_dependencies，因此那一分支保持 plain。
if (ROS2_WS_SOURCE_BUILD)
    if (NOT TARGET audio_io_interface)
        message(FATAL_ERROR "CLion workspace requires the in-tree audio_io_interface target.")
    endif ()
    if (NOT TARGET duck_audio_interface)
        message(FATAL_ERROR "CLion workspace requires the in-tree duck_audio_interface target.")
    endif ()
    target_include_directories(duck_audio PUBLIC
            ${rclcpp_INCLUDE_DIRS}
            ${sensor_msgs_INCLUDE_DIRS}
            ${std_msgs_INCLUDE_DIRS}
            ${geometry_msgs_INCLUDE_DIRS}
            ${tf2_ros_INCLUDE_DIRS}
            ${tf2_sensor_msgs_INCLUDE_DIRS})
    target_link_libraries(duck_audio PUBLIC
            ${rclcpp_LIBRARIES}
            ${sensor_msgs_LIBRARIES}
            ${std_msgs_LIBRARIES}
            ${geometry_msgs_LIBRARIES}
            ${tf2_ros_LIBRARIES}
            ${tf2_sensor_msgs_LIBRARIES})
    include("${audio_io_interface_SOURCE_DIR}/cmake_include.cmake")
    include("${duck_audio_interface_SOURCE_DIR}/cmake_include.cmake")
else ()
    find_package(audio_io_interface REQUIRED)
    find_package(duck_audio_interface REQUIRED)
    ament_target_dependencies(duck_audio PUBLIC
            rclcpp
            sensor_msgs
            std_msgs
            geometry_msgs
            tf2_ros
            tf2_sensor_msgs
            audio_io_interface
            duck_audio_interface)
endif ()

if (TARGET microduck_sounds)
    add_dependencies(duck_audio duck_audio_rust)
    target_link_libraries(duck_audio PRIVATE microduck_sounds)
    set_target_properties(duck_audio PROPERTIES
            BUILD_RPATH "${DUCK_RUST_TARGET_DIR}/release"
            INSTALL_RPATH "$ORIGIN")
endif ()

# ---------------------------------------------------------------------------
# Executable: duck_audio_node
# ---------------------------------------------------------------------------
if (BUILD_EXE)
    add_executable(duck_audio_node src/main.cpp)
    target_compile_features(duck_audio_node PRIVATE cxx_std_23)
    target_link_libraries(duck_audio_node duck_audio)
    if (ROS2_WS_SOURCE_BUILD)
        if (NOT TARGET common_cpp_ros_logging)
            message(FATAL_ERROR
                    "CLion workspace configuration requires the in-tree common_cpp_ros_logging target.")
        endif ()
        target_link_libraries(duck_audio_node common_cpp_ros_logging)
    else ()
        target_link_libraries(duck_audio_node common_cpp::common_cpp_ros_logging)
    endif ()
    if (TARGET microduck_sounds)
        add_dependencies(duck_audio_node duck_audio_rust)
        set_target_properties(duck_audio_node PROPERTIES
                BUILD_RPATH "${DUCK_RUST_TARGET_DIR}/release"
                INSTALL_RPATH "$ORIGIN/..")
    endif ()
endif ()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if (BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)
    ament_add_gtest(test_hand_tracker test/test_hand_tracker.cpp)
    target_link_libraries(test_hand_tracker duck_audio)
    ament_add_gtest(test_wav_pcm_reader test/test_wav_pcm_reader.cpp)
    target_link_libraries(test_wav_pcm_reader duck_audio)
    ament_add_gtest(test_audio_session test/test_audio_session.cpp)
    target_link_libraries(test_audio_session duck_audio)
endif ()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS duck_audio
        EXPORT duck_audioTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)
if (BUILD_EXE)
    install(TARGETS duck_audio_node DESTINATION lib/${PROJECT_NAME})
    install(DIRECTORY config launch
            DESTINATION share/${PROJECT_NAME}
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
    )
endif ()
if (TARGET microduck_sounds)
    install(FILES "${DUCK_RUST_LIB}" DESTINATION lib)
endif ()
if (DUCK_AUDIO_BUILD_RUST)
    # 与 ROS 2 节点相同的 lib/<pkg>/ 目录，可用 `ros2 run duck_audio sounds ...`。
    install(PROGRAMS "${DUCK_SOUNDS_BIN}" DESTINATION lib/${PROJECT_NAME})
elseif (DUCK_AUDIO_USE_PREBUILT_RUST)
    find_program(DUCK_SOUNDS_CLI NAMES sounds)
    if (DUCK_SOUNDS_CLI)
        install(PROGRAMS "${DUCK_SOUNDS_CLI}" DESTINATION lib/${PROJECT_NAME})
    endif ()
endif ()
install(DIRECTORY include/ DESTINATION include)

ament_export_include_directories(include)
ament_export_dependencies(rclcpp sensor_msgs std_msgs geometry_msgs tf2_ros tf2_sensor_msgs
        audio_io_interface duck_audio_interface common_cpp)
ament_export_targets(duck_audioTargets HAS_LIBRARY_TARGET)
ament_package()
