#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/rexglue-sdk" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="$(cd "$1" && pwd)"
patch_root="${repo_root}/patches/rexglue-sdk"
sdl_patch="0015-sdl-android-recreate-thread-barrier.patch"
sdl_root="${sdk_root}/thirdparty/sdl3"
sdl_revision="8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4"

if [[ ! -d "${sdl_root}/.git" && ! -f "${sdl_root}/.git" ]]; then
  echo "error: SDL3 submodule is not initialized: ${sdl_root}" >&2
  exit 1
fi

actual_sdl_revision="$(git -C "${sdl_root}" rev-parse HEAD)"
if [[ "${actual_sdl_revision}" != "${sdl_revision}" ]]; then
  echo "error: SDL3 revision ${actual_sdl_revision}; expected ${sdl_revision}" >&2
  exit 1
fi

while IFS= read -r patch_name; do
  if [[ "${patch_name}" == "${sdl_patch}" ]]; then
    git -C "${sdl_root}" apply --check --index -p1 "${patch_root}/${patch_name}"
    git -C "${sdl_root}" apply --index -p1 "${patch_root}/${patch_name}"
  else
    git -C "${sdk_root}" apply --check --index "${patch_root}/${patch_name}"
    git -C "${sdk_root}" apply --index "${patch_root}/${patch_name}"
  fi
done < "${patch_root}/series"
