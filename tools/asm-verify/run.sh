#!/usr/bin/env bash
# Host-side launcher: bind-mounts the project into the asm-verify container
# and invokes the in-container verify driver.
#
#   bash tools/asm-verify/run.sh
#
# Pre-requisite: bash tools/asm-verify/setup.sh (one-time image build).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE="${ASM_VERIFY_IMAGE:-fnverify-bada}"

# Normalise project root to a docker-friendly path. On Git Bash / MSYS the
# CWD looks like /c/Users/..., but docker (Rancher Desktop / Docker Desktop
# on Windows) wants C:/Users/... or //c/Users/... for bind-mounts.
to_docker_path() {
    local p="$1"
    if [[ "$p" =~ ^/([A-Za-z])/(.*)$ ]]; then
        local d="${BASH_REMATCH[1]^^}"
        printf '%s:/%s' "$d" "${BASH_REMATCH[2]}"
        return
    fi
    printf '%s' "$p"
}
PROJECT_ROOT_DOCKER="$(to_docker_path "$PROJECT_ROOT")"

if ! command -v docker > /dev/null; then
    echo "ERROR: docker not on PATH." >&2
    exit 1
fi

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "Image '$IMAGE' missing. Run tools/asm-verify/setup.sh first." >&2
    exit 1
fi

# Disable MSYS / Git Bash path translation so /work/... isn't rewritten
# to C:/Program Files/Git/work/... when handed to docker.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Optional filter: pass --filter <glob> or --class <Foo> or --symbol <name>
# to limit verification to a subset (lets you iterate on one class without
# re-running ~886 symbols).
FILTER=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter)  FILTER="$2"; shift 2 ;;
        --class)   FILTER="*${2}*"; shift 2 ;;          # loose substring on class
        --symbol)  FILTER="*${2}*"; shift 2 ;;          # loose substring on function
        --help|-h)
            cat <<USAGE
Usage: bash tools/asm-verify/run.sh [options]

Options:
  --filter <glob>    Run only symbols whose mangled name matches the glob.
                     Example: --filter '_ZN11WaveManager*'
  --class <Foo>      Shortcut for --filter '*Foo*' (loose substring match).
                     Example: --class WaveManager
  --symbol <name>    Same as --class but for one specific function name.
                     Example: --symbol GetNextWave

Without a filter, runs all symbols in tmp/asm-verify/manifest.generated.toml
(currently ~886 symbols across 110 portable TUs).
USAGE
            exit 0
            ;;
        *) echo "Unknown arg: $1 (try --help)" >&2; exit 2 ;;
    esac
done

# Bind-mount project (read-only) + tmpfs for build/cache.
# /staging uses a named volume (ext4) solely for the report extraction
# step — the source rsync inside verify.sh is fresh each run. /build and
# /cache are tmpfs, guaranteeing no stale .o files between runs.
docker run --rm \
    -e "ASM_VERIFY_FILTER=$FILTER" \
    -v "$PROJECT_ROOT_DOCKER:/work:ro" \
    -v fnverify-src:/staging \
    --tmpfs /build:exec,size=2G \
    --tmpfs /cache:exec,size=256M \
    "$IMAGE" -c 'bash /work/tools/asm-verify/verify.sh'

# verify.sh writes the report into /staging/tmp; lift it back out via a
# scratch container that mounts the named volume read-only.
docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    -v fnverify-src:/staging:ro \
    "$IMAGE" -c '
        mkdir -p /work/tmp/asm-verify
        cp /staging/tmp/asm-verify/report.md   /work/tmp/asm-verify/report.md
        cp /staging/tmp/asm-verify/report.json /work/tmp/asm-verify/report.json
    '

echo
echo "Report: tmp/asm-verify/report.md"
