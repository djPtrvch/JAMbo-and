# Передача работы следующей сессии

## Где мы
- Этап 1 (спайк) — готов. Итоги — docs/ai/audio-engine.md, link.md, midi.md.
- Этап 2 (перенос) — идёт. Решение пользователя: весь Core на C++ 1:1 по файлам `ios/JAMbo.swiftpm/Core` ветки swift djPtrvch/JAMbo (только читать); Kotlin + Compose — экран и системные API Android. Подробности переноса — docs/ai/core-port.md.
- **Core (аудиоядро, шаги 1–5) — готов.** Справочники, модель и файл проекта (JSON как у iPad), правки и UNDO, FX, движки, сэмплер, секвенсор, EQ, мастер, монитор, AudioCore в колбэке Oboe. Все тесты Swift-версии перенесены и проходят (gcc, clang, ASan/UBSan; эталоны golden 77/77). Попутно в последней сессии перенесены: правки EQ проекта (EqModel), Recording (запись нот с REC, RecordClock), Sounds (папка звуков, свой звук шага, запись на дорожку), ArrangeEdits целиком, ArrangeAuto (recordAuto, simplify), ArrangeRecorder (+ applyRecording, ownArrPatterns), Track::activate/moveSlot, WavetableKey (без parseUser).
- Не перенесено (не аудиоядро, по мере надобности): AppSettings/RecordSettings, ProjectSession (randomName, хранилище проектов на диске), EngineIcons, пресеты EQ и мастера (EqPresetBook, MasterPresetBook), правки варпинга Track (Warp.swift), SampleEdit, PhonesProfile/PhonesEq, HeadPack, переход MIX (Transition, MixTransition, Project::mixed).

## Шаг 6 — экран Compose (идёт)
Основной экран собран по Swift-версии; всё ниже собрано в CI (тесты Core проходят), **на устройстве не проверено** (кроме ранней раскладки на Pixel 7) — первое дело следующей сессии: спросить пользователя, что он увидел на планшете.

Как устроено:
- C++ `app/` — порт AppModel по частям: App.cpp/h (проект, UNDO, жесты, транспорт, ход игры), AppEdits (шаги, ручки, клавиатура), AppSound (меню и окно звука, стрелки папки), AppMod (LFO1–3/ENV на ручки), AppModPreview (миниатюры MOD), AppPages (миниатюры страниц, меню страницы, SPEED), AppFx (FX, MOD, SHIFT), AppMorph (MORPH), AppMeter + MeterState (измеритель), AppRec (REC). Тест tests/app_test.cpp (76 проверок).
- JNI `jni_app.cpp`: `act(code, a, b, x)` (коды — enum Act ↔ AppNative.Act) + данные по разделам (frame, top, tracks, steps, knobs, keys, sound, pages, side, mod; каждый кадр — meter, modNorms, icon, modMinis). Экран перечитывает разделы при смене ревизии. AudioEngine реализует AppAudio: modOut/modPhase/envOut, levels, clock, heardNow (время хоста блока передаётся в render; задержка выхода — updateLatency раз в секунду).
- Compose `ui/`: MainScreen (оверлеи меню звука и меню страницы), TopBar (BPM, метроном, UNDO, PLAY, REC, REDO; SCALE, ROOT, EQ, PATTERNS, ARRANGE — серые), LeftColumn, SoundMenu, SynthIcon, ModPreview, KnobGrid, RightColumn, PagesColumn, MeterColumn, TrackRow (жёлтая точка MORPH), SequencerRow, Keyboard, Layout, Theme. Бутерброд — пока экран спайка (MIDI, Link). Проект при запуске — 1 KICK, 2 ANALOG, 3 FM (временно).
- Раскладка (решения пользователя 20.11): точки iPad, высота экрана — 834 точки. Левая колонка — до правого края 3-го шага секвенсора, правая — от левого края 14-го (вместе с измерителем); середина — остаток (Layout.kt, Dims).
- Core, попутно: core/icons (иконки движков, сверка с ios/tools/icons/engine_previews_golden.json — 1054/1054), core/model/AnalogMod.cpp, core/model/MorphModes.cpp (+ tests/morph_test.cpp), App::publishAudio (таблицы WTABLE в снимок — без этого WTABLE молчал).

Сделано на экране: окно звука (имя, живая иконка, строки ручек), меню звука (движки дорожки и зажатых шагов, SAMPLER, TRACK), стрелки папки; LFO1–3/ENV (удержание + поворот, кольцо модуляции); миниатюры страниц (тап, протягивание, удержание, перенос, +, меню, SPEED, стрелки); FX и MOD (удержание / toggle — SharedPreferences «jambo», p-lock FX, LFO на FX, MOD-превью); SHIFT (тап по ручке — режимы, двойной тап — все p-lock); MORPH (тап, SHIFT + MORPH, MORPH + UNDO, возврат); измеритель (LUFS, пик, клиппинг, регулятор громкости); REC (запись нот при игре, дубль — одна запись UNDO).

Не перенесено (по ходу): слежение ударных за нотами (pitchKey), шаги Pitch/Start/End (stepByTap), свои значения по умолчанию (SHIFT + удержание), FX-картинки в окне, своя таблица WTABLE и GRANULAR по сэмплу (с библиотекой), стерео-сэмплы на метре, настройки (MORPH RETURN/HOLD, LEVEL RELATIVE, QUANTIZE/OVERDUB — пока по умолчанию), writeLead без лидера (lead_ = 0 до сетки паттернов), меню дорожки в MIX.

## Дальше — по пунктам (решение пользователя 20.11: идём по порядку)
1. ~~SCALE и ROOT, CHORD, меню клавиатуры~~ — сделано (AppPiano.cpp, core/model/Chords, PianoMenus.kt; на устройстве не проверено): SCALE/ROOT — палец вверх-вниз 24 dp на пункт, тап — меню; CHORD — тап вкл./выкл. (у дорожки свой, не в проекте), долгое нажатие — окно типов, подписи клавиш «Em · i»; меню под CHORD — 1/2 OCTAVES, SWING, KEY VELOCITY (сила по месту на клавише — акселерометра iPad нет), OVERDUB (SharedPreferences). Не перенесено: ARP (ARPEGGIATOR, окно ARP), RANDOM (SHIFT + CHORD, стили ударных, ансамбль), раздел PART, Capture (две стрелки октавы).
2. ~~Меню бутерброда~~ — сделано (на устройстве не проверено): MainMenu.kt (PROJECT, APP, CONTROLLERS; MIDI и SYNC — пока экран спайка), PROJECTS (ProjectsMenu.kt; C++ — core/model/ProjectStore — проекты в filesDir/Projects, ProjectSession, app/AppProjects.cpp: Autosave через 1,5 с и при onStop, NEW, SAVE со случайным именем, SAVE AS с вопросом о перезаписи, EDIT и корзина свайпом, LOAD; последний проект — файл current в папке), SETTINGS (SettingsMenu.kt: RECORD — QUANTIZE, MORPH — MODE и RETURN, LEVEL, APPEARANCE — тема, AUDIO ENGINE — строка Now). Не перенесено: EXPORT/IMPORT .jambo (ProjectPack, выбор файла Android), MIX (переход), IMPORT строк, сэмплы проекта с диска (вместе с библиотекой), цвет тональности по квинтовому кругу (KeyMatch), вкладки SETTINGS для ещё не перенесённых функций.
3. ~~Сетка паттернов~~ — сделано (на устройстве не проверено): core/model/Clips.cpp (весь Clips.swift: копия, вставка, очистка клипов, строки + тесты LaunchTests и testRowEditsFollowBlocks), app/AppGrid.cpp (лидер, запуск при игре командой в колбэк и без игры сразу, ячейки, строки, окно строк, ▲ ▼ VIEW/SWITCH, ход ожиданий и строк из колбэка), PatternGrid.kt + PatternMenus.kt + GridData.kt (ячейки с миниатюрой, полоса хода, мигание ожидающих, колонка строк с повторами, «+», бутерброд сетки, LOOP ROW, стрелки; жесты — тап, удержание, перенос клипа и строки, свайп вверх — меню ячейки, двойной тап по строке — меню строки, прокрутка колонки строк). Верхняя панель: SCALE и EQ шириной с PATTERNS. Не перенесено: кнопка STEPS (скрыть секвенсор и клавиатуру), рамка выделения, TO ARRANGE и запись аранжировки, переход MIX (PREV), MIDI-цели ячеек и строк, плавные сдвиги строк при переносе.
4. ~~EQ~~ — сделано (на устройстве не проверено): app/AppEq.cpp (канал, правки полос — жест одной записью UNDO, BYPASS, точки съёма спектров, сдвиги DYN), app/EqDisplay (порт SpectrumDisplay и EqPlot.curves), EqView.kt (строка полей по кнопкам верхней панели — topRects; TYPE, FREQ, GAIN, Q, M/S — движение, тап — меню, двойной тап — ввод числом; DYN — меню источника с THRESHOLD и RANGE; BYPASS), EqPlot.kt (сетка, спектры, кривые стерео/MID/SIDE, точки, мультитач, двойной тап — новая полоса или вкл./выкл., подсказка над пальцем). Не перенесено: LISTEN, PRESETS (EqPresetBook), сравнение дорожек (COMPARE), коррекция наушников (CORRECT), спектр конечного сигнала мастера.
   Колонки экрана (решение пользователя 21.11): левая — до правого края первой клавиши, правая — от левого края последней, по клавишам одной октавы (при 2 OCTAVES не сужаются).
5. Библиотека сэмплов и загрузка сэмплов (LOAD в меню звука, форма волны в окне).
6. Окна MIX и MST в боковых колонках (полосы MIX, индикация мастера, скоп, кнопки мастера).

## Последняя правка (21.11, не проверена на устройстве)
- Шрифты: `tsp()` и `LocalTextScale` (Theme.kt) — на телефоне ×1,2 (имена дорожек, клавиши, ручки, верхняя панель); на
  планшете — ×1, пользователь посмотрит и скажет.
- Верхняя панель: SCALE, ROOT, UNDO и зеркально REDO, пианоролл, EQ — одной ширины (weight); SCALE и ROOT — в одну
  строку; ▶ и ● крупнее. Подробности экрана — docs/ai/ui.md.
- Первое дело следующей сессии: спросить, как это выглядит на Pixel и на планшете; затем пункт 5 (библиотека и
  загрузка сэмплов).

## Решения пользователя
- Формат файла проекта — тот же, что у iPad-версии (совместимость в обе стороны).
- Кнопку CLICK (щелчок Link) убрали: метроном — своей кнопкой.
- Время в отчётах — по Москве.
- Проверки (docs/ai/build-ci.md, 20.11): тесты Core в CI — только при правках `app/src/main/cpp/`, `tests/`, `scripts/`, `.github/`; APK — на каждый push. Небольшие правки (экран, подписи, цвета, документы) — коммит и push без ожидания CI; ошибку сборки пришлёт пользователь.

## Как работать
- **CI не ждать после небольших правок** (кнопки, раскладка, подписи, документы) — решение пользователя, повторено 21.11; ждать — только после глобальных правок C++ и тестов.
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
