#!/usr/bin/env bash
set -euo pipefail
# Run from a Claude cloud environment setup field or the repo checkout.
missing=()
for tool in python3 c++ cmake ninja rg; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if ((${#missing[@]})); then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "Install Python 3, g++, CMake, Ninja, and ripgrep, then retry." >&2
        exit 1
    fi
    elevate=()
    if ((EUID != 0)); then elevate=(sudo); fi
    "${elevate[@]}" apt-get update -qq
    "${elevate[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq python3 g++ cmake ninja-build ripgrep
fi
echo "Cloud tools ready. Run bash tools/cloud-check.sh from the repository root."
