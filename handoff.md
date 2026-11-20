# Передача работы следующей сессии

## Где мы
- Этап 1 (спайк) — готов. Итоги — docs/ai/audio-engine.md, link.md, midi.md.
- Этап 2 (перенос) — идёт. Решение пользователя: весь Core на C++ 1:1 по файлам `ios/JAMbo.swiftpm/Core` ветки swift djPtrvch/JAMbo (только читать); Kotlin + Compose — экран и системные API Android. Подробности переноса — docs/ai/core-port.md.
- **Core (аудиоядро, шаги 1–5) — готов.** Справочники, модель и файл проекта (JSON как у iPad), правки и UNDO, FX, движки, сэмплер, секвенсор, EQ, мастер, монитор, AudioCore в колбэке Oboe. Все тесты Swift-версии перенесены и проходят (gcc, clang, ASan/UBSan; эталоны golden 77/77). Попутно в последней сессии перенесены: правки EQ проекта (EqModel), Recording (запись нот с REC, RecordClock), Sounds (папка звуков, свой звук шага, запись на дорожку), ArrangeEdits целиком, ArrangeAuto (recordAuto, simplify), ArrangeRecorder (+ applyRecording, ownArrPatterns), Track::activate/moveSlot, WavetableKey (без parseUser).
- Не перенесено (не аудиоядро, по мере надобности): AppSettings/RecordSettings, ProjectSession (randomName, хранилище проектов на диске), EngineIcons, пресеты EQ и мастера (EqPresetBook, MasterPresetBook), правки варпинга Track (Warp.swift), SampleEdit, PhonesProfile/PhonesEq, HeadPack, переход MIX (Transition, MixTransition, Project::mixed).

## Шаг 6 — экран Compose (идёт)
- C++ `app/` (App.h, App.cpp, AppEdits.cpp — порт части AppModel): проект, UNDO (жесты), выбор дорожки/MIX/MST, свайп mute/solo, шаги (тап, протягивание, копия, длина паттерна), ручки с p-lock (двойной тап — снять / к значению по умолчанию), клавиатура (одна октава + верхняя корневая, как Settings.pianoOctaves = 1), ноты MIDI — на выбранную дорожку, ход игры из очереди секвенсора. Тест tests/app_test.cpp (22).
- JNI `jni_app.cpp`: `act(code, a, b, x)` (коды — enum Act ↔ AppNative.Act) + данные по разделам (frame, top, tracks, steps, knobs, keys + подписи); экран перечитывает разделы только при смене ревизии. AudioEngine реализует jambo::AppAudio (audio_bridge.h).
- Compose `ui/`: MainScreen, TopBar (BPM, метроном, UNDO, PLAY на касании, REDO, ROLL — значок клавиш; SCALE, ROOT, REC, ROLL, EQ, PATTERNS, ARRANGE — серые), KnobGrid, TrackRow, SequencerRow, Keyboard, Theme, Layout. Бутерброд — пока экран спайка (MIDI, Link) с BACK. Проект при запуске — 1 KICK, 2 ANALOG, 3 FM (временно, пока нет меню звука).
- Масштаб (решения пользователя 20.11): раскладка в точках iPad, высота экрана — всегда 834 точки (LocalDensity = px на точку). Android-планшеты шире iPad (16:10, 16:9) — тупо растягивать нельзя: середина (сетка ручек и кнопки над ней) — по размерам iPad; лишняя ширина — боковым колонкам поровну (не шире 360); над левой колонкой растёт поле BPM, над правой — PATTERNS и ARRANGE (ровно над колонкой); остаток (очень широкий экран, Pixel 7) — середине. Шаги и клавиши — во всю ширину, высота клавиатуры — как на iPad. Код — Layout.kt (Dims, column(), mid()).
- Проверено пользователем на Pixel 7: всё видно, ручки с кольцами (скриншот после масштаба). Последняя версия раскладки (Dims) на устройстве ещё не проверена.
- Дальше (предложено пользователю, выбор за ним): 1) левая колонка — окно звука и меню звука (выбор движка); 2) правая колонка — миниатюры страниц, MORPH, FX, SHIFT, MOD; 3) REC — запись нот (модель уже есть); измеритель уровня. В App::edit добавить ownArrPatterns, как в Swift (AppModel.edit).

## Решения пользователя
- Формат файла проекта — тот же, что у iPad-версии (совместимость в обе стороны).
- Кнопку CLICK (щелчок Link) убрали: метроном — своей кнопкой.
- Время в отчётах — по Москве.
- Проверки (docs/ai/build-ci.md, 20.11): тесты Core в CI — только при правках `app/src/main/cpp/`, `tests/`, `scripts/`, `.github/`; APK — на каждый push. Небольшие правки (экран, подписи, цвета, документы) — коммит и push без ожидания CI; ошибку сборки пришлёт пользователь.

## Как работать
- **Сессию открывать только с репозиторием JAMbo-and** (без JAMbo: его CLAUDE.md на рабочей ветке — 286 КБ, ~85 тыс. токенов в контексте). Исходники Swift-версии: `scripts/fetch-swift-src.sh`, затем `git -C third_party/jambo-swift.git show swift:<путь>` (список — `ls-tree -r --name-only swift <папка>`).
- Тесты Core: `scripts/fetch-test-deps.sh`, затем `cmake -S tests -B build/tests && cmake --build build/tests -j8 && build/tests/core_tests` (новый .cpp — заново `cmake -S tests -B build/tests` и строка в tests/CMakeLists.txt и tests/main.cpp).
- Проверять и в clang (как NDK): `CXX=clang++ cmake -S tests -B build/clang && cmake --build build/clang && build/clang/core_tests`. В CI — gcc 15: явные `<cstdint>` и т. п.; clang ловит «vexing parse» — `std::vector<T> x(static_cast<size_t>(n))`, не `x(size_t(n))`.
- CI собирает тесты в `-j "$(nproc)"` (без числа раннер зависал от нехватки памяти), предел 20 мин.
- APK — только в CI (Android SDK в сессии нет); ссылка — docs/ai/build-ci.md. Kotlin локально не проверить: JNI-файлы можно проверить `clang++ -fsyntax-only` с заголовками JDK (`/usr/lib/jvm/java-21-openjdk-amd64/include`) и Oboe (в прежнем scratchpad, иначе скачать).
- Порт: те же имена, константы и порядок вычислений; Double там, где Double в Swift; без -ffast-math.

## Открыто
- Копия Project для UNDO — полная (в Swift — дешёвая); если будет медленно — делить общие неизменяемые части.
- TouchOSC (iPad) по ADVERTISE: в примере нота 3 (~10 Hz, неслышна) — пользователь проверит с нотой 48–72.
- Задержка смены темпа Pixel → Drambo — особенность Drambo; строку в docs/ai/link.md предложено дописать, не подтверждено.
- Пик GRANULAR на крайних ручках до +6,5 dBFS и режимы модуляции ANALOG (SYNC, FM, RING, AM) — сверить со Swift на слух.
