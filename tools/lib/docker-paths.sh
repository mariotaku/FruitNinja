#!/usr/bin/env bash
# Shared helper: normalise an MSYS/Git-Bash path (/c/Users/...) to a
# docker-friendly path (C:/Users/... ) for bind-mounts.
#
# Used by tools/asm-verify/run.sh and tools/symbol-diff/run.sh -- both
# invoke docker from Git Bash / MSYS on Windows, where docker (Rancher
# Desktop / Docker Desktop) expects C:/Users/... or //c/Users/... rather
# than the MSYS /c/Users/... form.
#
# Usage:
#   source "$(dirname "$0")/../lib/docker-paths.sh"
#   PROJECT_ROOT_DOCKER="$(to_docker_path "$PROJECT_ROOT")"

to_docker_path() {
    local p="$1"
    if [[ "$p" =~ ^/([A-Za-z])/(.*)$ ]]; then
        local d="${BASH_REMATCH[1]^^}"
        printf '%s:/%s' "$d" "${BASH_REMATCH[2]}"
        return
    fi
    printf '%s' "$p"
}
