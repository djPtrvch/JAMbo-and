# Передача работы следующей сессии

## Где мы
- Этап 1 (спайк) — готов. Итоги в docs/ai/audio-engine.md, link.md, midi.md.
- Этап 2 (перенос Core) — идёт. Решение пользователя (вариант A): весь Core на C++ (звук, секвенсор, модель проекта, правки, UNDO, сохранение, MIDI-маппинг), Kotlin + Compose — только экран и системные API Android. Записано в CLAUDE.md.
- Перенесено на C++ и проверено (подробно — docs/ai/core-port.md):
  - FX: Delay, Reverb, Drive (+ DriveTables), Duck — 18 эталонов fx_*, совпадают (−102…−315 dB).
  - Движки: KICK, ANALOG, FM, WTABLE, SUB (JamboVoice, JamboAnalog), SNARE, HAT, CLAP, PERC (JamboDrums), TEXTURE, GRANULAR (+ TexGran), JamboTables, встроенные wavetable — 18 эталонов j16–j19 совпадают; ударные, TEXTURE, GRANULAR — тесты свойств.
  - Без проверки: SUB, режимы модуляции ANALOG (SYNC, FM, RING, AM); пик GRANULAR на крайних ручках до +6,5 dBFS — сверить со Swift на слух.
- В звук приложения перенесённое ещё не подключено (APK — экран спайка).

## Решения пользователя (этой сессии)
- Весь Core — C++ 1:1 по файлам `ios/JAMbo.swiftpm/Core` ветки swift djPtrvch/JAMbo (только читать).
- Формат файла проекта — **тот же, что у iPad-версии** (совместимость в обе стороны): ProjectCoding переносится 1:1, проверка — на настоящих файлах проектов.
- Кнопку CLICK (щелчок Link) убрали: метроном будет своей кнопкой.

## План первой вехи (порядок)
1. ~~Справочники~~ — готово: Params (+Fmt), SynthEngines, FilterType, Scales; KnobDefaults (значения, apply, JSON), Track.setSynth и миграции Filter — с шагом 2.
2. Данные проекта: Step, Clip, Track (с TrackMore), Project. Поля, не нужные в первой вехе (аранжировщик, EQ, ARP, MIDI дорожки), — заводятся сразу без логики, чтобы файл читался целиком.
3. Чтение и запись проекта (ProjectCoding, ProjectRepair) — JSON как у iPad; тесты на файлах из Swift-тестов (ProjectFileTests и др.).
4. Правки и UNDO: TrackEdits, RollEdits, History — с тестами Swift-версии (EditTests и др.).
5. Аудиоядро: SoundSnapshot, SeqSnapshot, AudioCore, Sequencer, SynthVoices (+ JamboBank, LadderFilter, Biquad, ModEngine), Sampler, микшер — в колбэк Oboe.
6. Экран Compose: дорожки, 16 шагов, PLAY, пианоролл, сетка ручек 4×4 — по UI Swift-версии. Граница с C++ — JNI: «выполни действие» и «дай данные для отрисовки»; перерисовка — только изменившегося (CPU спайка 40 % — из-за перерисовки всего экрана).
7. APK: шаги + PLAY — играют синты и сэмплы.

## Как работать
- Тесты Core: `scripts/fetch-test-deps.sh`, затем `cmake -S tests -B build/tests && cmake --build build/tests && build/tests/core_tests` (новый .cpp — заново `cmake -S tests -B build/tests`).
- Сборка APK — только в CI (Android SDK в облачной сессии недоступен); логи — GitHub Actions. Ссылка на APK — docs/ai/build-ci.md.
- Порт: те же имена, константы и порядок вычислений; Double там, где Double в Swift; без -ffast-math.

## Открыто
- TouchOSC (iPad) по ADVERTISE: в примере нота 3 (~10 Hz, неслышна) — пользователь проверит с нотой 48–72.
- Задержка смены темпа Pixel → Drambo — особенность Drambo (Loopy Pro и AUM — без задержки); строку в docs/ai/link.md предложено дописать, не подтверждено.
