# Сборка и CI
- Каждый push → `.github/workflows/android.yml`: JDK 17, Gradle 8.11.1, `scripts/fetch-deps.sh` (Link 4.1.1 + asio), `gradle assembleDebug`.
- APK → pre-release `debug-<ветка>` (`/` → `-`), файл `jambo-debug.apk`; копия — артефакт запуска.
- Подпись — `app/debug.keystore` (пароль android, не секрет): APK ставится поверх старого.
- Номер сборки = GITHUB_RUN_NUMBER (versionName 0.1.N, виден на экране).
- В облачной сессии Android SDK недоступен — сборка проверяется только в CI.
- Тесты Core в CI (задание core-tests) — только при правках `app/src/main/cpp/`, `tests/`, `scripts/` или `.github/`; APK — на каждый push. Сборка тестов — `-j "$(nproc)"` (без числа раннер зависает от нехватки памяти), предел — 20 мин на задание.
- Небольшие правки (экран, подписи, цвета, документы) — коммит и push без ожидания CI; ошибку сборки, если будет, пришлёт пользователь (решение пользователя, 20.11).
