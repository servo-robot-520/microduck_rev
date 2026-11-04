#!/usr/bin/env bash
# 把上游 `sounds` crate 复制到 src/duck_audio/rust/sounds。
#
# 声音就是身份：crate 源码、乐谱和产生它们的 commit 必须一起固定，每只机器人的 seed 都依赖
# 精确的 RNG 与合成代码。这个脚本把 pin 写清楚，而不是手工编辑 checkout。
#
# 用法：
#   tools/sync_microduck_sounds.sh                 # pinned commit below
#   tools/sync_microduck_sounds.sh --latest        # resolve origin/HEAD now (and print the SHA)
#   tools/sync_microduck_sounds.sh --commit SHA    # explicit full commit
#   tools/sync_microduck_sounds.sh --dry-run
#
# After a sync, run:
#   cd rust && cargo test -p sounds
#   cd rust && cargo build --release --locked -p microduck_sounds_ffi
set -euo pipefail

URL="${MICRODUCK_URL:-https://github.com/pollen-robotics/microduck.git}"
# 固定的上游 commit。`--latest` 只更新本次 checkout，不改这个值：发布必须能仅凭脚本复现。
# 提交生成的 `rust/UPSTREAM_MICRODUCK_COMMIT`；确实要整体换声时再改这个默认值。
PINNED_COMMIT="d13be2c1a2f4b1e0b0e499b08b92f870518b1384"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/rust/sounds"
STAMP="${ROOT}/rust/UPSTREAM_MICRODUCK_COMMIT"
REF=""
DRY_RUN=0

usage() {
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
}

while (($#)); do
    case "$1" in
        --commit)
            [[ $# -ge 2 ]] || { echo "--commit needs a SHA" >&2; exit 2; }
            REF="$2"; shift 2 ;;
        --latest)
            REF="HEAD"; shift ;;
        --url)
            [[ $# -ge 2 ]] || { echo "--url needs a URL" >&2; exit 2; }
            URL="$2"; shift 2 ;;
        --dest)
            [[ $# -ge 2 ]] || { echo "--dest needs a directory" >&2; exit 2; }
            DEST="$2"; shift 2 ;;
        --dry-run)
            DRY_RUN=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -n "$REF" ]]; then
    if [[ "$REF" == "HEAD" || ! "$REF" =~ ^[0-9a-f]{40}$ ]]; then
        echo "resolving ${REF} from ${URL}" >&2
        REF="$(git ls-remote --exit-code "$URL" "$REF" | awk 'NR==1 {print $1}')"
        [[ -n "$REF" ]] || { echo "cannot resolve ${REF:-$1}" >&2; exit 1; }
    fi
else
    REF="$PINNED_COMMIT"
fi

if [[ ! "$REF" =~ ^[0-9a-f]{40}$ ]]; then
    echo "resolved ref is not a full commit: $REF" >&2
    exit 1
fi

echo "upstream: ${URL}"
echo "commit:   ${REF}"
echo "dest:     ${DEST}"
if ((DRY_RUN)); then
    exit 0
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/microduck-sounds.XXXXXX")"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

# 窄拉取：只取一个 commit，不要历史。`git archive` 只解出 `sounds/`，因此无关 crate 和
# 仅供测试的受版权乐谱不会进入，除非它们本来就在 sounds 里。
git init -q "$TMP/repo"
git -C "$TMP/repo" remote add origin "$URL"
git -C "$TMP/repo" fetch -q --depth 1 origin "$REF"
GOT="$(git -C "$TMP/repo" rev-parse FETCH_HEAD)"
if [[ "$GOT" != "$REF" ]]; then
    echo "fetched ${GOT}, expected ${REF}" >&2
    exit 1
fi

mkdir -p "$TMP/extract"
git -C "$TMP/repo" archive --format=tar "$REF" sounds | tar -x -C "$TMP/extract"
[[ -f "$TMP/extract/sounds/Cargo.toml" ]] || {
    echo "upstream commit ${REF} has no sounds/Cargo.toml" >&2
    exit 1
}

# 在目标旁 staging 再替换，避免中断留下半个 crate。
mkdir -p "$(dirname "$DEST")"
STAGE="$(mktemp -d "$(dirname "$DEST")/.sounds-stage.XXXXXX")"
trap 'rm -rf "$TMP" "$STAGE"' EXIT
cp -a "$TMP/extract/sounds/." "$STAGE/"
rm -rf "$DEST"
mv "$STAGE" "$DEST"
printf '%s\n' "$REF" > "$STAMP"

echo "copied sounds/ from ${REF}"
echo "wrote ${STAMP}"
echo "next: (cd ${ROOT}/rust && cargo test -p sounds && cargo build --release --locked -p microduck_sounds_ffi)"
