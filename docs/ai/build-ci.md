# Сборка и CI
- Каждый push → `.github/workflows/android.yml`: JDK 17, Gradle 8.11.1, `scripts/fetch-deps.sh` (Link 4.1.1 + asio), `gradle assembleDebug`.
- APK → pre-release `debug-<ветка>` (`/` → `-`), файл `jambo-debug.apk`; копия — артефакт запуска.
- Подпись — `app/debug.keystore` (пароль android, не секрет): APK ставится поверх старого.
- Номер сборки = GITHUB_RUN_NUMBER (versionName 0.1.N, виден на экране).
- В облачной сессии Android SDK недоступен — сборка проверяется только в CI.
