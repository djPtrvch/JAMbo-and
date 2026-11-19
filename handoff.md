# Передача работы следующей сессии

## Где мы
- Этап 1 (спайк) — готов. Итоги в docs/ai/audio-engine.md, link.md, midi.md.
- Этап 2 (перенос Core) — идёт. Решение пользователя (вариант A): весь Core на C++ (звук, секвенсор, модель проекта, правки, UNDO, сохранение, MIDI-маппинг), Kotlin + Compose — только экран и системные API Android. Записано в CLAUDE.md.
- Перенесено на C++ и проверено (подробно — docs/ai/core-port.md):
  - FX: Delay, Reverb, Drive (+ DriveTables), Duck — 18 эталонов fx_*, совпадают (−102…−315 dB).
  - Движки: KICK, ANALOG, FM, WTABLE, SUB (JamboVoice, JamboAnalog), SNARE, HAT, CLAP, PERC (JamboDrums), TEXTURE, GRANULAR (+ TexGran), JamboTables, встроенные wavetable — 18 эталонов j16–j19 совпадают; ударные, TEXTURE, GRANULAR — тесты свойств.
  - Справочники, модель проекта, файл проекта (JSON как у iPad, repair, миграции версий 3–10) — тесты tables_test, model_test, coding_test.
  - Без проверки: SUB, режимы модуляции ANALOG (SYNC, FM, RING, AM); пик GRANULAR на крайних ручках до +6,5 dBFS — сверить со Swift на слух.
- APK звучит ядром: кнопка PLAY CORE (проверочный проект) и ноты MIDI; экран — пока спайк.

## Решения пользователя (этой сессии)
- Весь Core — C++ 1:1 по файлам `ios/JAMbo.swiftpm/Core` ветки swift djPtrvch/JAMbo (только читать).
- Формат файла проекта — **тот же, что у iPad-версии** (совместимость в обе стороны): ProjectCoding переносится 1:1, проверка — на настоящих файлах проектов.
- Кнопку CLICK (щелчок Link) убрали: метроном будет своей кнопкой.
- Время в отчётах (CI и др.) — по Москве.

## План первой вехи (порядок)
1. ~~Справочники~~ — готово (tables_test.cpp): Params (+Fmt), SynthEngines, FilterType, Scales.
2. ~~Данные проекта~~ — готово (model_test.cpp): Step, Clip, Track, Project и типы полей (поля TrackMore/ProjectMore — обычные поля); KnobDefaults, Track::setSynth, Project::migrateFilter.
3. ~~Чтение и запись проекта~~ — готово (coding_test.cpp): ProjectFormat «jambo-swift», model/Json.h — правила Swift Codable, ProjectDecode/ProjectEncode, ProjectRepair и миграции, пресеты синта и KnobDefaults в JSON. Хранилище проектов на диске (ProjectStore) — позже. Проверено на файлах по образцу Swift-тестов; настоящий файл с iPad — ждём от пользователя.
4. ~~Правки и UNDO~~ — готово (edit_test.cpp): History (место правки — std::any), TrackEdits (страницы, шаги, p-lock, MORPH, дорожки проекта), RollEdits и Roll, StepNotes; части Clips (fillRows), Transition (звук дорожки), remapEqDyn, morphFxId. Тесты EditTests и MorphTests; шкалы параметров — в tables_test. Не перенесено: ArrangeEdits целиком (есть часть), остальное Clips и Transition (переход MIX).
5. Аудиоядро — основное готово (5a–5d4c); осталась часть тестов через ядро (5d4b, см. ниже), можно параллельно с шагом 6.
   - ~~5a~~ — util (SPSCQueue, AtomicFloat, CPUMeter), Biquad, LadderFilter, SoundSnapshot (+ SampleData, SoundKey, Slices, EqSpec::fill/mixOrder, MasterChain::fill, ParamScale, ModTarget, VoiceParam), ModEngine, EnvDot; для TSTR и WARP — TempoDetect, Onsets (SampleData::loopBpm, onsets), WarpMap, метки варпинга в снимке (putWarp) — тест audio_test.cpp (+ TempoDetectTests, WarpTests: карта, AUTO WARP, снимок).
   - ~~5b~~ — JamboBank, SynthVoices (+Envelope, +Notes), PitchKey, MULTICORE: RenderPool (POSIX-семафоры, std::thread; stop() дожидается потоков) — тест synth_test.cpp (SynthTests напрямую через SynthVoices, RenderPoolTests; гонок нет — ThreadSanitizer).
   - ~~5c~~ — SeqData/SeqSnapshot, PlayClock, TempoSource, Click, ArrAutoPlay, Launcher (+ArrangePlay), SeqOut, Sequencer, стемы: StemRing, StemCollector, SequencerStems — тест seq_test.cpp (+ StemTests: звук в кольцо кладёт сам тест, микшер — с сэмплером). Сэмплер для секвенсора — интерфейс SeqVoices (Sampler его реализует).
   - Тесты Swift-версии, которым нужен AudioCore (ModTests, SynthTests, SequencerTests, StemTests, RenderPoolTests со звуком ядра), перенесены на уровне узлов; целиком — после 5d4.
   - Не перенесены (не аудиоядро, по мере надобности): правки варпинга Track (autoWarp, addWarp, setWarpBpm — Model/Warp.swift), SampleEdit (редактор сэмпла).
   - 5d — делить так:
     - ~~5d1~~ — готово (sampler_test.cpp, 65 проверок): Sampler (голос, SamplerNotes, SamplerMix, SamplerMidi + MidiNoteOut, SamplerPreview, SamplerSynth, SamplerWarp — голосовая часть SoundWarp), таймстретч BEAT (Stretcher) и NORMAL (Vocoder, PvFFT, PvVec — ветка без Accelerate), NoteLocks; FxRack (перенесён сюда из 5d2 — нужен микшеру). Sampler реализует SeqVoices. Тесты: SamplerTests (без SampleEdit/PitchDetect/папок Track — не аудиоядро), RateTests (звук), WarpTests (звук; метки заданы вручную — правки варпинга Track не перенесены), PvFFTTests, VocoderLoadTests.
     - ~~5d2~~ — готово: EqCoeffs, EqUnit (+M/S, +Dynamic), EqDetectors, EqDynLevels, EqListen, Spectrum (SpectrumTap, Spectra, RealFFT, SpectrumAnalyzer), TrackEqs, Levels (+Loudness), TrackTaps, RenderProfile; SamplerHooks.cpp — настоящие EQ дорожек, метры, спектры, замеры. Эталоны eq_* 22 из 22 (eq_golden.cpp), тест eq_test.cpp (31 проверка: EqListen, спектр, LUFS/EBU 3341, EQ на сухом сигнале и посылах, хвост, Dynamic от дорожки, пики до mute, съём спектров). Не перенесено: пресеты EQ (EqPresetBook), правки EQ проекта (addEqBand и др.), PhonesEq — не аудиоядро дорожек.
     - ~~5d3~~ — готово: MasterTables, MasterSaturator, MasterGlue (+AUTO MAKEUP), MasterClipper, MasterLimiter (+GUARD), DynTele, MasterScope; MasterChain — класс (модули по unique_ptr, заголовок лёгкий), UNITY и громкость выхода. Эталоны mst_* 19 из 19 (master_golden.cpp; вход самотестов — golden::masterInput), тест master_test.cpp (12 проверок). DriveTables::makeTable стал публичным. Не перенесено: пресеты мастера (MasterPresetBook), MixTransition.blendMaster.
     - 5d4 — делить так (AudioCore.swift тянет ещё узлы):
       - ~~5d4a~~ — готово: Arpeggiator (+константы Arp), TestTone, Capture (+RecPeaks), RefPlayer; Headphone Monitor — monitor/: MonitorDesign (MonitorSpec, MonitorSettings, MonitorProfile, TranslationPreset, расчёт параметров), RoomIR (+RoomInfo, RoomResponse, parseWav), RoomConv + RoomStage (экземпляры — сырыми указателями через очереди, освобождает drain), MonitorChain. Тесты arp_test.cpp (12), monitor_test.cpp (28; ресурсы комнат — ios/JAMbo.swiftpm/Resources через fetch-test-deps.sh). Не перенесено: HeadPack (zip-пакеты голов — с библиотекой, S10), PhonesProfile/PhonesEq (профили наушников, пресеты EQ), хранение MonitorSettings.
       - **5d4b (идёт):** ~~AudioCore + CoreEvent~~ — готово (engine/AudioCore, engine/CoreEvent; снимки — сырыми указателями через очереди, освобождает releaseRetired(now) через 0,5 с; голоса синта — у ядра, installSynth). Тесты через ядро: core_test.cpp (20: сэмплер, задержка мастера 219 и BYPASS, уровни, p-lock MIX, телеметрия), launch_test.cpp (10: LaunchTests без перехода MIX), fxroute_test.cpp (10: FxRoutingTests), mod_core_test.cpp (5: ModTests). **Осталось** (можно параллельно с 5d4c): SequencerTests (с ядром), SynthTests, StemTests, RenderPoolTests, ArrangePlay/Own/AutoReturn, ChordSteal, DeleteTracksPlaying, StepNotes, Piano, Tone, TempoSync, Ref, Capture, KickDrive, SubEngine, TexGranVoice, FilterType (с ядром), EqTests/EqDynTests (с ядром), MonitorTests.testExportBitExact и testRefRoutingMatrix, LevelsTests.testScopeTaps.
       - 5d4c (идёт): ~~AudioCore в колбэке Oboe~~ — сделано (audio_engine.cpp: ядро блоками ≤ 4096, планарно → чередование; тон и TAP спайка — поверх; события и снимки — под мьютексом producer-стороны produce_, колбэк без блокировок). Ноты MIDI спайка → дорожка 2 ядра (ANALOG), кнопка PLAY CORE — проверочный проект (бочка DRUM + бас ANALOG; та же проверка — core_test «проверочный проект APK»). Проверено на Pixel 7 (19.11): PLAY CORE и ноты MIDI звучат. Щелчки были от XRun (колбэк 52 / 95 %, буфер 4 мс) — исправлено: AudioStream::setPerformanceHintEnabled (ADPF) + oboe::LatencyTuner; стало 17 / 38 %, буфер 192 кадра (4 мс), XRuns 0, задержка выхода 9,7 мс. CPU app ~50 % — перерисовка экрана спайка. Дальше — JNI «выполни действие / дай данные» для экрана Compose (шаг 6).
6. **Следующий:** экран Compose (ждёт подтверждения пользователя): дорожки, 16 шагов, PLAY, пианоролл, сетка ручек 4×4 — по UI Swift-версии. Граница с C++ — JNI: «выполни действие» и «дай данные для отрисовки»; перерисовка — только изменившегося (CPU спайка 40 % — из-за перерисовки всего экрана).
7. APK: шаги + PLAY — играют синты и сэмплы.

## Как работать
- Тесты Core: `scripts/fetch-test-deps.sh`, затем `cmake -S tests -B build/tests && cmake --build build/tests && build/tests/core_tests` (новый .cpp — заново `cmake -S tests -B build/tests`).
- Сборка APK — только в CI (Android SDK в облачной сессии недоступен); логи — GitHub Actions. Ссылка на APK — docs/ai/build-ci.md.
- Проверять и в clang (как NDK): `CXX=clang++ cmake -S tests -B build/clang && cmake --build build/clang && build/clang/core_tests`; в CI тесты — gcc 15 (строже к include: `<cstdint>` и др. — явно).
- После push смотреть CI (GitHub Actions: core-tests и build), не только локальные тесты.
- Порт: те же имена, константы и порядок вычислений; Double там, где Double в Swift; без -ffast-math.

## Открыто
- Копия Project для UNDO — полная (в Swift — дешёвая, «коробки»); если на шаге 4 будет медленно — делить общие неизменяемые части.
- TouchOSC (iPad) по ADVERTISE: в примере нота 3 (~10 Hz, неслышна) — пользователь проверит с нотой 48–72.
- Задержка смены темпа Pixel → Drambo — особенность Drambo (Loopy Pro и AUM — без задержки); строку в docs/ai/link.md предложено дописать, не подтверждено.
