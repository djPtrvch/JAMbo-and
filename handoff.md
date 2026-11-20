# Передача работы следующей сессии

## Где мы
- Этап 1 (спайк) — готов. Итоги — docs/ai/audio-engine.md, link.md, midi.md.
- Этап 2 (перенос) — идёт. Решение пользователя: весь Core на C++ 1:1 по файлам `ios/JAMbo.swiftpm/Core` ветки swift djPtrvch/JAMbo (только читать); Kotlin + Compose — экран и системные API Android. Подробности переноса — docs/ai/core-port.md.
- **Core (аудиоядро, шаги 1–5) — готов.** Справочники, модель и файл проекта (JSON как у iPad), правки и UNDO, FX, движки, сэмплер, секвенсор, EQ, мастер, монитор, AudioCore в колбэке Oboe. Все тесты Swift-версии перенесены и проходят (gcc, clang, ASan/UBSan; эталоны golden 77/77). Попутно в последней сессии перенесены: правки EQ проекта (EqModel), Recording (запись нот с REC, RecordClock), Sounds (папка звуков, свой звук шага, запись на дорожку), ArrangeEdits целиком, ArrangeAuto (recordAuto, simplify), ArrangeRecorder (+ applyRecording, ownArrPatterns), Track::activate/moveSlot, WavetableKey (без parseUser).
- Перенесено 21.11: Chords (без random и writeProgression), ProjectStore на диске и ProjectSession (+ randomName), Clips.swift целиком (строки и клипы).
- Не перенесено (не аудиоядро, по мере надобности): AppSettings/RecordSettings (настройки — у экрана, SharedPreferences), правки варпинга Track (Warp.swift), SampleEdit, PhonesProfile/PhonesEq, HeadPack, переход MIX (Transition, MixTransition, Project::mixed).

## Шаг 6 — экран Compose (идёт)
Основной экран собран по Swift-версии и проверяется пользователем на планшете и Pixel 7.

Как устроено:
- C++ `app/` — порт AppModel по частям: App.cpp/h (проект, UNDO, жесты, транспорт, ход игры), AppEdits (шаги, ручки, клавиатура), AppSound (меню и окно звука, стрелки папки), AppMod (LFO1–3/ENV на ручки), AppModPreview (миниатюры MOD), AppPages (миниатюры страниц, меню страницы, SPEED), AppFx (FX, MOD, SHIFT), AppMorph (MORPH), AppMeter + MeterState (измеритель), AppRec (REC), AppPiano (гамма, нота, CHORD, меню клавиатуры, SWING), AppProjects (проекты, Autosave), AppGrid (сетка паттернов, лидер), AppEq + EqDisplay (EQ, спектры, кривые). Тест tests/app_test.cpp (109 проверок), tests/chord_test.cpp, tests/store_test.cpp.
- JNI `jni_app.cpp`: `act(code, a, b, x)` (коды — enum Act ↔ AppNative.Act) + данные по разделам (frame, top, tracks, steps, knobs, keys, piano, sound, pages, side, mod, projects, settings, grid, eq; строки — projectAct, setScene, eqSetBand и т. п.; каждый кадр — meter, modNorms, icon, modMinis, gridLive, eqSpectrum, eqCurves). Экран перечитывает разделы при смене ревизии. AudioEngine реализует AppAudio: modOut/modPhase/envOut, levels, clock, heardNow (время хоста блока передаётся в render; задержка выхода — updateLatency раз в секунду).
- Compose `ui/`: MainScreen (оверлеи меню), TopBar (BPM, метроном, SCALE, ROOT, UNDO, PLAY, REC, REDO, EQ, PATTERNS; ARRANGE и пианоролл — серые), PianoMenus, MainMenu, MenuParts, ProjectsMenu, SettingsMenu, PatternGrid, PatternMenus, GridData, EqView, EqPlot, LeftColumn, SoundMenu, SynthIcon, ModPreview, KnobGrid, RightColumn, PagesColumn, MeterColumn, TrackRow (жёлтая точка MORPH), SequencerRow, Keyboard, Layout, Theme. Бутерброд — своё меню (MIDI и SYNC — пока экран спайка). Стартовый проект 1 KICK, 2 ANALOG, 3 FM — только при первом запуске (нет проектов).
- Раскладка: точки iPad, высота экрана — 834 точки; колонки — по клавише одной октавы (21.11), подробно — docs/ai/ui.md.
- Core, попутно: core/icons (иконки движков, сверка с ios/tools/icons/engine_previews_golden.json — 1054/1054), core/model/AnalogMod.cpp, core/model/MorphModes.cpp (+ tests/morph_test.cpp), App::publishAudio (таблицы WTABLE в снимок — без этого WTABLE молчал).

Сделано на экране: окно звука (имя, живая иконка, строки ручек), меню звука (движки дорожки и зажатых шагов, SAMPLER, TRACK), стрелки папки; LFO1–3/ENV (удержание + поворот, кольцо модуляции); миниатюры страниц (тап, протягивание, удержание, перенос, +, меню, SPEED, стрелки); FX и MOD (удержание / toggle — SharedPreferences «jambo», p-lock FX, LFO на FX, MOD-превью); SHIFT (тап по ручке — режимы, двойной тап — все p-lock); MORPH (тап, SHIFT + MORPH, MORPH + UNDO, возврат); измеритель (LUFS, пик, клиппинг, регулятор громкости); REC (запись нот при игре, дубль — одна запись UNDO).

Не перенесено (по ходу): слежение ударных за нотами (pitchKey), шаги Pitch/Start/End (stepByTap), свои значения по умолчанию (SHIFT + удержание), FX-картинки в окне, своя таблица WTABLE и GRANULAR по сэмплу (с библиотекой), стерео-сэмплы на метре.

## Дальше — по пунктам (решение пользователя 20.11: идём по порядку)
1. ~~SCALE и ROOT, CHORD, меню клавиатуры~~ — сделано (AppPiano.cpp, core/model/Chords, PianoMenus.kt; на устройстве не проверено): SCALE/ROOT — палец вверх-вниз 24 dp на пункт, тап — меню; CHORD — тап вкл./выкл. (у дорожки свой, не в проекте), долгое нажатие — окно типов, подписи клавиш «Em · i»; меню под CHORD — 1/2 OCTAVES, SWING, KEY VELOCITY (сила по месту на клавише — акселерометра iPad нет), OVERDUB (SharedPreferences). Не перенесено: ARP (ARPEGGIATOR, окно ARP), RANDOM (SHIFT + CHORD, стили ударных, ансамбль), раздел PART, Capture (две стрелки октавы).
2. ~~Меню бутерброда~~ — сделано (на устройстве не проверено): MainMenu.kt (PROJECT, APP, CONTROLLERS; MIDI и SYNC — пока экран спайка), PROJECTS (ProjectsMenu.kt; C++ — core/model/ProjectStore — проекты в filesDir/Projects, ProjectSession, app/AppProjects.cpp: Autosave через 1,5 с и при onStop, NEW, SAVE со случайным именем, SAVE AS с вопросом о перезаписи, EDIT и корзина свайпом, LOAD; последний проект — файл current в папке), SETTINGS (SettingsMenu.kt: RECORD — QUANTIZE, MORPH — MODE и RETURN, LEVEL, APPEARANCE — тема, AUDIO ENGINE — строка Now). Не перенесено: EXPORT/IMPORT .jambo (ProjectPack, выбор файла Android), MIX (переход), IMPORT строк, сэмплы проекта с диска (вместе с библиотекой), цвет тональности по квинтовому кругу (KeyMatch), вкладки SETTINGS для ещё не перенесённых функций.
3. ~~Сетка паттернов~~ — сделано (на устройстве не проверено): core/model/Clips.cpp (весь Clips.swift: копия, вставка, очистка клипов, строки + тесты LaunchTests и testRowEditsFollowBlocks), app/AppGrid.cpp (лидер, запуск при игре командой в колбэк и без игры сразу, ячейки, строки, окно строк, ▲ ▼ VIEW/SWITCH, ход ожиданий и строк из колбэка), PatternGrid.kt + PatternMenus.kt + GridData.kt (ячейки с миниатюрой, полоса хода, мигание ожидающих, колонка строк с повторами, «+», бутерброд сетки, LOOP ROW, стрелки; жесты — тап, удержание, перенос клипа и строки, свайп вверх — меню ячейки, двойной тап по строке — меню строки, прокрутка колонки строк). Верхняя панель: SCALE и EQ шириной с PATTERNS. Не перенесено: кнопка STEPS (скрыть секвенсор и клавиатуру), рамка выделения, TO ARRANGE и запись аранжировки, переход MIX (PREV), MIDI-цели ячеек и строк, плавные сдвиги строк при переносе.
4. ~~EQ~~ — сделано (на устройстве не проверено): app/AppEq.cpp (канал, правки полос — жест одной записью UNDO, BYPASS, точки съёма спектров, сдвиги DYN), app/EqDisplay (порт SpectrumDisplay и EqPlot.curves), EqView.kt (строка полей по кнопкам верхней панели — topRects; TYPE, FREQ, GAIN, Q, M/S — движение, тап — меню, двойной тап — ввод числом; DYN — меню источника с THRESHOLD и RANGE; BYPASS), EqPlot.kt (сетка, спектры, кривые стерео/MID/SIDE, точки, мультитач, двойной тап — новая полоса или вкл./выкл., подсказка над пальцем). Не перенесено: LISTEN, PRESETS (EqPresetBook), сравнение дорожек (COMPARE), коррекция наушников (CORRECT), спектр конечного сигнала мастера.
   Колонки экрана (решение пользователя 21.11): левая — до правого края первой клавиши, правая — от левого края последней, по клавишам одной октавы (при 2 OCTAVES не сужаются).
5. ~~Библиотека сэмплов и загрузка~~ — сделано, проверено пользователем (22.11): C++ core/model/Library, core/files (Sha256, AudioFile, Resample, SampleStore), core/sampler/Pitch, app/AppLibrary.cpp; JNI — jni_lib.cpp (jni_common.h — мьютекс и App); экран — LibraryView, LibraryRow, MediaDecode; LOAD и SAVE пресета в меню звука; форма волны, полосы воспроизведения и затемнение Start/End в окне сэмпла. Не перенесено: SAVE сэмпла (с редактором), EXPORT, .zip, LOAD TABLE и свои таблицы, REF, SOURCE, перетаскивание папок, marks, CLEAR SAMPLE LOCKS (всегда ON).
6. ~~MIX и MST в боковых колонках, окна мастера, микшер, запись~~ — сделано, проверено пользователем (22.11):
   - C++: AppMaster.cpp (BYPASS, скоп, вид полос MIX, анализ), MasterMeters.h, ScopeState, MixAnalysis (FREQ, MASK, LOUD), MixStereo (BandStereo, LoudHistory), AppMasterWin.cpp + MasterWin (окна модулей: поля, AUTO MAKEUP, UNITY, пресеты модулей, SAVE DEFAULT, история телеметрии, спектры сатуратора; пресеты EQ и LISTEN; двойной тап по MIX/MST — закрыть окно), AppMix.cpp (M, S, ●, меню дорожки, перестановка, запись стемов и ● MASTER в RECORDINGS); core/model MasterInput, MasterPresets, EqPresets; synth/TrackRotate.h (перестановка на ходу без провала звука). JNI — jni_master.cpp, jni_masterwin.cpp.
   - Экран: MasterViews (индикация, кнопки мастера, скоп), MasterWindow + MasterDisplays (окна SAT, COMP, CLIP, LIM), MixViews (обзор MIX), MixStereoViews, MixerWindow, MixCells (ячейки MIX: метр, M, S, ●, меню дорожки), EqPresets (меню PRESETS EQ), SETTINGS → APPEARANCE → MASTER FX DISPLAY SCALE.
   - Решения пользователя 22.11: LISTEN сбрасывается при закрытии EQ или смене канала; новая дорожка сразу играет свой клип; подъём ручки при перестановке — как было (с первым движением после 0,5 с); иконка приложения — J (res/mipmap*, буква уменьшена).
7. Следующие этапы (решение пользователя 22.11 — делать в следующей сессии, по порядку):
   1. Редактор сэмпла: маркеры нарезки, TRIM, NORMALIZE, MONO, запись сэмпла (REC), SAVE сэмпла в библиотеку.
   2. Меню MIDI и SYNC вместо экрана спайка: маппинг, LEARN, Twister, MIDI-клок.
   3. Окно AUTO (автоматизация).
   4. EXPORT/IMPORT проекта .jambo.
   5. ARP и RANDOM.
   6. Аранжировщик.

## Последняя правка (22.11)
- Всё выше собрано в CI (последняя проверенная сборка — 127); правки после неё (иконка меньше, MASTER в EQ, область LISTEN, меньший значок LOOP, сброс LISTEN) — без проверки CI по решению пользователя.
- Первое дело следующей сессии: этап 7.1 — редактор сэмпла.

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
