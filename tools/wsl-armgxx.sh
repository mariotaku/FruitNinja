#!/usr/bin/env bash
# C++ variant of wsl-armgcc.sh -- see that script for the rationale.
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
TC_BIN="${project_root}/bada_SDK/Tools/Toolchains/ARM/bin/arm-bada-eabi-g++.exe"

if [[ ! -x "$TC_BIN" ]]; then
  echo "wsl-armgxx.sh: bada SDK toolchain missing at $TC_BIN" >&2
  exit 127
fi

export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

to_winpath_raw() {
  local p="$1"
  if [[ "$p" =~ ^/([A-Za-z])/(.*)$ ]]; then
    local d="${BASH_REMATCH[1]}"
    printf '%s:/%s' "${d^^}" "${BASH_REMATCH[2]}"
    return
  fi
  printf '%s' "$p"
}

to_winpath() {
  local a="$1"
  if [[ "$a" =~ ^(-I|-L|-B|-iquote|-iprefix)/([A-Za-z])/(.*)$ ]]; then
    local flag="${BASH_REMATCH[1]}"
    local d="${BASH_REMATCH[2]}"
    printf '%s%s:/%s' "$flag" "${d^^}" "${BASH_REMATCH[3]}"
    return
  fi
  to_winpath_raw "$a"
}

args=()
for a in "$@"; do
  args+=("$(to_winpath "$a")")
done

exec "$TC_BIN" "${args[@]}"
