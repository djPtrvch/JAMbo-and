#!/usr/bin/env bash
# Ableton Link (C++, GPLv2) с подмодулем asio — в third_party/link (не в git).
set -euo pipefail
cd "$(dirname "$0")/.."
LINK_TAG=Link-4.1.1
if [ ! -f third_party/link/AbletonLinkConfig.cmake ]; then
  git clone --depth 1 -b "$LINK_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/Ableton/link third_party/link
fi
