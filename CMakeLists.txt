cmake_minimum_required(VERSION 3.14)
set(PROJECT_VERSION 1.0.0)
project("microduck_rev")

# 顶层启用 CTest：聚合构建下各子包的 ament_add_gtest 才能在根构建目录注册测试。
include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

if (DEFINED ENV{CLION_IDE})
    message(STATUS "CLion env detected")
endif ()

function(require_cxx23_stdlib)
    set(CMAKE_CXX_STANDARD 23 PARENT_SCOPE)
    set(CMAKE_CXX_STANDARD_REQUIRED ON PARENT_SCOPE)

    set(_saved_quiet "${CMAKE_REQUIRED_QUIET}")
    set(_saved_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_QUIET TRUE)
    string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++23")

    include(CheckCXXSourceCompiles)

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
  ]] _INTERNAL_HAVE_CXX23_STDLIB)

    set(CMAKE_REQUIRED_QUIET "${_saved_quiet}")
    set(CMAKE_REQUIRED_FLAGS "${_saved_flags}")

    if (NOT _INTERNAL_HAVE_CXX23_STDLIB)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            execute_process(
                    COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
                    OUTPUT_VARIABLE _gcc_ver OUTPUT_STRIP_TRAILING_WHITESPACE)
            message(FATAL_ERROR
                    "\n===========================================\n"
                    "  \"${PROJECT_NAME}\" requires C++23 stdlib support\n"
                    "  (<expected>, <format>, <ranges>)\n"
                    "-------------------------------------------\n"
                    "  Detected : GCC ${_gcc_ver}\n"
                    "  Required : GCC 13+\n"
                    "===========================================\n\n"
                    "  Solutions:\n\n"
                    "     Install GCC 13+ (Ubuntu):\n"
                    "     sudo apt update\n"
                    "     sudo apt install software-properties-common\n"
                    "     sudo add-apt-repository ppa:ubuntu-toolchain-r/test\n"
                    "     sudo apt update\n"
                    "     sudo apt install gcc-13 g++-13\n\n"
                    "===========================================\n")
        else ()
            message(FATAL_ERROR
                    "\n===========================================\n"
                    "  \"${PROJECT_NAME}\" requires C++23 stdlib support\n"
                    "  (<expected>, <format>, <ranges>)\n"
                    "-------------------------------------------\n"
                    "  Current compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
                    "  Please use GCC 13+ with libc++.\n"
                    "===========================================\n")
        endif ()
    endif ()

    message(STATUS "${PROJECT_NAME}: C++23 stdlib support confirmed")
endfunction()

function(colcon_add_subdirectories)
    cmake_parse_arguments(PARSE_ARGV 0 "ARG" "" "BUILD_BASE;BASE_PATHS" "")

    execute_process(COMMAND colcon list
            --paths-only
            --base-paths ${ARG_BASE_PATHS}
            --topological-order
            ${ARG_UNPARSED_ARGUMENTS}
            OUTPUT_VARIABLE paths)
    string(STRIP "${paths}" paths)
    string(REPLACE "\n" ";" paths "${paths}")

    set(colcon_cmake_package_paths)
    set(colcon_cmake_package_names)
    set(colcon_build_rpaths)
    foreach (path IN LISTS paths)
        execute_process(COMMAND colcon info --paths "${path}" OUTPUT_VARIABLE package_info)
        if (NOT "${package_info}" MATCHES "type:[ \t]+(cmake|ros.ament_cmake|ros.cmake)")
            message("skipping non-cmake project")
        elseif (NOT "${package_info}" MATCHES "name:[ \t]+([^ \r\n\t]*)")
            message(WARNING "could not identify package at ${path}")
        else ()
            set(name "${CMAKE_MATCH_1}")
            get_filename_component(BUILD_PATH "${name}" ABSOLUTE BASE_DIR "${ARG_BUILD_BASE}")
            list(APPEND colcon_cmake_package_paths "${path}")
            list(APPEND colcon_cmake_package_names "${name}")
            list(APPEND colcon_build_rpaths "${BUILD_PATH}")
        endif ()
    endforeach ()

    if (colcon_build_rpaths)
        list(APPEND CMAKE_BUILD_RPATH ${colcon_build_rpaths})
        list(REMOVE_DUPLICATES CMAKE_BUILD_RPATH)
        list(LENGTH colcon_build_rpaths colcon_build_rpath_count)
        message(STATUS
                "Source workspace build: added ${colcon_build_rpath_count} package output directories "
                "to CMAKE_BUILD_RPATH")
    endif ()

    list(LENGTH colcon_cmake_package_paths colcon_cmake_package_count)
    if (colcon_cmake_package_count GREATER 0)
        math(EXPR colcon_cmake_package_last_index "${colcon_cmake_package_count} - 1")
        foreach (index RANGE ${colcon_cmake_package_last_index})
            list(GET colcon_cmake_package_paths ${index} path)
            list(GET colcon_cmake_package_names ${index} name)
            get_filename_component(BUILD_PATH "${name}" ABSOLUTE BASE_DIR "${ARG_BUILD_BASE}")
            add_subdirectory("${path}" "${BUILD_PATH}")
        endforeach ()
    endif ()
endfunction()

function(check_colcon_build_dir)
    get_filename_component(_colcon_build_base
            "${CMAKE_SOURCE_DIR}/build" REALPATH)
    get_filename_component(_root_cmake_binary_dir
            "${CMAKE_BINARY_DIR}" REALPATH)

    file(RELATIVE_PATH _relative
            "${_colcon_build_base}" "${_root_cmake_binary_dir}")

    if (_relative STREQUAL "." OR NOT _relative MATCHES "^\\.\\.")
        message(FATAL_ERROR
                "Root CMake binary directory '${CMAKE_BINARY_DIR}' is inside colcon's "
                "workspace build directory '${CMAKE_SOURCE_DIR}/build'.\n"
                "Configure CLion with a separate build directory, for example "
                "'${CMAKE_SOURCE_DIR}/cmake-build-debug'.")
    endif ()

    message(STATUS "Colcon build dir check passed")
endfunction()

require_cxx23_stdlib()
check_colcon_build_dir()
# 标识顶层源码聚合构建。
set(ROS2_WS_SOURCE_BUILD ON)
# 仅用于 CLion 的代码分析和调试。
colcon_add_subdirectories(
        BUILD_BASE "${CMAKE_BINARY_DIR}/colcon_packages"
        BASE_PATHS "${PROJECT_SOURCE_DIR}/src/"
        --packages-select
        common_cpp
        audio_io_interface
        duck_audio_interface
        duck_ear
        tts_interface
        mpu6500_ros2_control
        feetech_servo_ros2_control
        battery_monitor
        audio_io
#        tts
        duck_audio
)
