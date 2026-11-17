#!/bin/bash
# 安装 Orbbec 相机 ROS2 驱动（OrbbecSDK_ROS2）——duck_bringup 部署用一键安装脚本。
#
# 产物为 colcon install-base 布局：<目标>/setup.bash、<目标>/orbbec_camera 等，
# 驱动功能包路径不写死在任何 launch/config 中，安装并 source 后由 ament 索引解析。
set -e
# 官方: https://github.com/orbbec/OrbbecSDK_ROS2
REPO_URL="${ORBBEC_ROS2_REPO:-https://github.com/servo-robot-520/OrbbecSDK_ROS2.git}"
BRANCH="${ORBBEC_ROS2_BRANCH:-v1.5.22}"
TARGET=""
FULL_HISTORY=0

PACKAGES=(orbbec_camera orbbec_camera_msgs orbbec_description)

usage() {
    cat <<EOF
Usage: install_orbbec_camera.sh [目标目录] [options]

Install OrbbecSDK_ROS2 (${PACKAGES[*]}) into <目标目录> as a colcon install prefix.

Options:
  -b, --branch REF   Branch or tag to clone (default: v1.5.22，本机型验证过的驱动版本；
                     注意仓库默认分支 v2-main 的 launch 布局不兼容).
  -r, --remote URL   Git URL of OrbbecSDK_ROS2 (default: $REPO_URL).
  -p, --proxy URL    HTTP proxy for git (e.g. http://127.0.0.1:7897)。
  --full             Clone full history (default: shallow clone, latest commit only).
  -h, --help         Show this help message.

Default target: ~/ros2_pkgs

After installation:
  source <目标目录>/setup.bash     # bash
  source <目标目录>/setup.zsh      # zsh
  ros2 launch duck_bringup orbbec_camera.launch.py

Notes:
  源码克隆到 <目标目录>/.src/OrbbecSDK_ROS2，构建产物缓存在 .build/.log，
  重复执行仅增量构建；已有克隆时自动 git pull 更新。
EOF
    exit 0
}

die() {
    echo "Error: $*" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--branch)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            BRANCH="$2"
            shift 2
            ;;
        -r|--remote)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            REPO_URL="$2"
            shift 2
            ;;
        -p|--proxy)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            export http_proxy="$2" https_proxy="$2" HTTP_PROXY="$2" HTTPS_PROXY="$2"
            shift 2
            ;;
        --full)
            FULL_HISTORY=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        -*)
            echo "Error: unknown option '$1'" >&2
            usage
            ;;
        *)
            [[ -z "$TARGET" ]] || die "multiple target directories given: $TARGET and $1"
            TARGET="$1"
            shift
            ;;
    esac
done
TARGET="${TARGET:-$HOME/ros2_pkgs}"

mkdir -p "$TARGET"
TARGET="$(cd "$TARGET" && pwd)"
[[ "$TARGET" != "/" ]] || die "target must not be the filesystem root"

command -v git >/dev/null 2>&1 || die "git not found"
command -v colcon >/dev/null 2>&1 || die "colcon not found (先安装 ROS 2 与 colcon)"

# ============================================================
# ROS 2 环境
# ============================================================
load_ros_env() {
    local distro="${ROS_DISTRO:-humble}"
    local setup="/opt/ros/$distro/setup.bash"
    [[ -f "$setup" ]] || die "ROS 2 environment not found: $setup (is ROS_DISTRO set correctly?)"
    # shellcheck disable=SC1090
    source "$setup"
}

# ============================================================
# 源码：克隆 / 更新
# ============================================================
resolve_source() {
    SRC="$TARGET/.src/OrbbecSDK_ROS2"
    if [[ -d "$SRC/.git" ]]; then
        echo "Updating existing checkout: $SRC"
        git -c http.version=HTTP/1.1 -C "$SRC" fetch --all --prune ||
            echo "Warning: git fetch failed (network/proxy?), using existing checkout"
        if [[ -n "$BRANCH" ]]; then
            git -C "$SRC" checkout --quiet "$BRANCH"
        fi
        git -c http.version=HTTP/1.1 -C "$SRC" pull --ff-only ||
            echo "Warning: cannot fast-forward $SRC, keeping the current checkout"
        return 0
    fi
    echo "Cloning $REPO_URL -> $SRC"
    mkdir -p "$(dirname "$SRC")"
    # 弱网/代理下大传输易被截断，默认浅克隆（仅最新提交，足够构建）；
    # --full 先尝试完整历史，失败自动降级浅克隆。
    local -a full=(git -c http.version=HTTP/1.1 clone)
    local -a shallow=(git -c http.version=HTTP/1.1 clone --depth 1 --single-branch)
    if [[ -n "$BRANCH" ]]; then
        full+=(--branch "$BRANCH")
        shallow+=(--branch "$BRANCH")
    fi
    if [[ "$FULL_HISTORY" == 1 ]]; then
        if ! "${full[@]}" "$REPO_URL" "$SRC"; then
            rm -rf "$SRC"
            echo "Warning: full clone failed, retrying shallow clone ..."
            "${shallow[@]}" "$REPO_URL" "$SRC"
        fi
    else
        "${shallow[@]}" "$REPO_URL" "$SRC"
    fi
    [[ -f "$SRC/orbbec_camera/package.xml" ]] ||
        die "cloned repository does not contain orbbec_camera/package.xml"
}

# ============================================================
# 构建并安装
# ============================================================
build_and_install() {
    echo "Building with colcon (Release) ..."
    (
        cd "$TARGET"
        colcon --log-base "$TARGET/.log" build \
            --base-paths "$SRC" \
            --install-base "$TARGET" \
            --build-base "$TARGET/.build" \
            --cmake-args -DCMAKE_BUILD_TYPE=Release
    ) || die "colcon build failed"
}

verify_install() {
    local pkg
    for pkg in "${PACKAGES[@]}"; do
        [[ -d "$TARGET/$pkg" ]] || die "install incomplete: missing $TARGET/$pkg"
    done
    [[ -f "$TARGET/setup.bash" ]] || die "install incomplete: missing $TARGET/setup.bash"
    [[ -d "$TARGET/orbbec_camera/lib" ]] ||
        die "install incomplete: missing $TARGET/orbbec_camera/lib"
}

# ============================================================
# udev 规则（相机设备访问权限），失败不阻塞安装
# ============================================================
install_udev_rules() {
    local rules="$TARGET/orbbec_camera/share/orbbec_camera/udev/99-obsensor-libusb.rules"
    local dst="/etc/udev/rules.d/99-obsensor-libusb.rules"
    [[ -f "$rules" ]] || { echo "Warning: udev rules not found: $rules"; return 0; }
    if [[ -f "$dst" ]] && cmp -s "$rules" "$dst"; then
        echo "udev rules already installed and up to date"
        return 0
    fi
    if [[ -t 0 ]] && command -v sudo >/dev/null 2>&1; then
        echo "Installing udev rules (sudo) ..."
        sudo cp "$rules" "$dst" && sudo udevadm control --reload-rules && sudo udevadm trigger ||
            echo "Warning: udev rules install failed, please install manually:"
    else
        echo "udev rules not installed (no terminal/sudo). Install manually:"
    fi
    [[ -f "$dst" ]] && cmp -s "$rules" "$dst" && return 0
    echo "  sudo cp $rules $dst"
    echo "  sudo udevadm control --reload-rules && sudo udevadm trigger"
}

load_ros_env
resolve_source
build_and_install
verify_install
install_udev_rules

echo ""
echo "Installed ${PACKAGES[*]}"
echo "  target : $TARGET"
echo "  source : $SRC"
echo ""
echo "Environment:"
echo "  source $TARGET/setup.bash   # bash"
echo "  source $TARGET/setup.zsh    # zsh"
echo ""
echo "Launch:"
echo "  ros2 launch duck_bringup orbbec_camera.launch.py"
