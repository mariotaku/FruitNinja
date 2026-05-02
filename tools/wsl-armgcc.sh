#!/usr/bin/env bash
# Cross-compiler wrapper, ARM Thumb-2 / VFPv3 / hard-float.
#
# Phase A uses the bada SDK 4.5.3 native Win32 toolchain (i686-mingw32),
# because the 4.4.1 i386-Linux toolchain via WSL fails to stat() files on
# /c/... drvfs mounts (32-bit inode overflow). 4.5.3 vs 4.4.1 codegen
# drift is small (cosmetic, mostly tail-call elision) -- acceptable for
# the verifier's normalization layer to swallow.
#
# Phase B can swap to true 4.4.1 once a 64-bit Linux prebuilt is sourced
# or sources are staged through WSL native fs.

set -euo pipefail

# Resolve the script's directory in /c/ form so we hit the SDK reliably.
script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
TC_BIN="${project_root}/bada_SDK/Tools/Toolchains/ARM/bin/arm-bada-eabi-gcc.exe"

if [[ ! -x "$TC_BIN" ]]; then
  echo "wsl-armgcc.sh: bada SDK toolchain missing at $TC_BIN" >&2
  exit 127
fi

# Disable MSYS Git Bash path mangling so /c/... and /home/... pass through.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Convert MSYS-style /c/... or /C/... to Windows C:/... for the Win32 toolchain.
to_winpath_raw() {
  # /c/foo or /C/foo  ->  C:/foo .  Anything else echoes unchanged.
  local p="$1"
  if [[ "$p" =~ ^/([A-Za-z])/(.*)$ ]]; then
    local d="${BASH_REMATCH[1]}"
    printf '%s:/%s' "${d^^}" "${BASH_REMATCH[2]}"
    return
  fi
  printf '%s' "$p"
}

# Translate paths inside common compiler-flag prefixes too:
#   -I/c/...  -> -IC:/...
#   -isystem /c/...  (preserved separately as the next arg)
#   -L, -B, -isysroot, -iquote, etc.
to_winpath() {
  local a="$1"
  case "$a" in
    -I/?/*|-L/?/*|-B/?/*|-iquote/?/*|-iprefix/?/*)
      local prefix="${a%%/*/*}"
      # Above splits off the LAST /X/Y; instead use awk-style:
      ;;
  esac
  # Easier approach: handle the flag-with-attached-path forms explicitly.
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
