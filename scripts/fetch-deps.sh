#!/usr/bin/env bash
# Ableton Link (C++, GPLv2) с подмодулем asio — в third_party/link (не в git).
set -euo pipefail
cd "$(dirname "$0")/.."
LINK_TAG=Link-4.1.1
if [ ! -f third_party/link/AbletonLinkConfig.cmake ]; then
  git clone --depth 1 -b "$LINK_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/Ableton/link third_party/link
fi
# JSON (nlohmann, MIT): чтение и запись проекта (ProjectCoding).
if [ ! -f third_party/json/single_include/nlohmann/json.hpp ]; then
  mkdir -p third_party
  rm -rf third_party/json
  git clone --depth 1 -b v3.11.3 --filter=blob:none --sparse https://github.com/nlohmann/json third_party/json
  git -C third_party/json sparse-checkout set single_include
fi
