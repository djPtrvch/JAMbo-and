#!/usr/bin/env bash
# Исходники Swift-версии (djPtrvch/JAMbo, ветка swift — только чтение) для переноса: «голый» клон без рабочих файлов
# в third_party/jambo-swift.git (не в git). Читать так: git -C third_party/jambo-swift.git show swift:ios/CLAUDE.md,
# список файлов — git -C third_party/jambo-swift.git ls-tree -r --name-only swift ios/JAMbo.swiftpm/Core.
# Без рабочих файлов CLAUDE.md репозитория JAMbo не попадает в контекст сам (решение пользователя 20.11).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party
if [ ! -d third_party/jambo-swift.git ]; then
  git clone --bare --filter=blob:none --single-branch -b swift https://github.com/djPtrvch/JAMbo third_party/jambo-swift.git
else
  git -C third_party/jambo-swift.git fetch -q origin swift:swift
fi
git -C third_party/jambo-swift.git log --oneline -1 swift
