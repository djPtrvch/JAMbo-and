#!/usr/bin/env bash
# Для тестов Core на Linux: эталоны из djPtrvch/JAMbo (ветка swift, ios/CoreTests/golden — только чтение)
# и nlohmann/json (разбор *_cases.json). Всё — в third_party (не в git).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party
if [ ! -d third_party/jambo-golden/ios/CoreTests/golden ]; then
  rm -rf third_party/jambo-golden
  git clone --depth 1 -b swift --filter=blob:none --sparse https://github.com/djPtrvch/JAMbo third_party/jambo-golden
  git -C third_party/jambo-golden sparse-checkout set ios/CoreTests/golden
fi
if [ ! -f third_party/json/single_include/nlohmann/json.hpp ]; then
  rm -rf third_party/json
  git clone --depth 1 -b v3.11.3 --filter=blob:none --sparse https://github.com/nlohmann/json third_party/json
  git -C third_party/json sparse-checkout set single_include
fi
