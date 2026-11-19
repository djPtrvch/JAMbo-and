# Перенос Core
- Звук и всё, что в колбэке, — C++17 в `app/src/main/cpp/core/`, 1:1 по файлам `ios/JAMbo.swiftpm/Core` (ветка swift JAMbo): те же имена, константы и порядок вычислений; Double там, где Double в Swift.
- Проверка — `tests/` на Linux: сверка с `ios/CoreTests/golden` (скачивает `scripts/fetch-test-deps.sh`, только чтение), критерий как в Swift: по отсчётам ≤ −60 dB или третьоктавные полосы ±0,5 dB; в CI — задача `core-tests`.
- Без `-ffast-math` (проверки NaN и совпадение с эталонами).
- Перенесено: FX/Delay (4 эталона, −102…−315 dB).
