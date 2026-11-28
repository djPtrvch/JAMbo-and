# Архитектура JAMbo Android

`app/src/main/java/app/jambo/android/` содержит Kotlin, Android API и Compose в `ui/`. В `app/src/main/cpp/` находится Android-обвязка, `app/` — контроллер C++ `App`, `core/` — музыкальная модель и аудиоядро C++17. Имена файлов C++ ниже даны относительно этого каталога. Хостовые тесты находятся в [tests/](tests/).

```text
Касание Compose / android.media.midi → Kotlin → JNI → App / Project
                                                      ↓ CoreEvent и снимки
Oboe AudioEngine::onAudioReady ← буферы ← AudioCore ← Sequencer / Launcher
                                            ↓ атомарные данные и очереди
                                   App::tick → ScreenState → Compose
```

## Состояние, UNDO и хранение

- `MainActivity.kt` открывает хранилище и управляет жизненным циклом аудио и MIDI. При потере экрана отпускаются экранные ноты, при уходе в фон записывается Autosave.
- `App.cpp` и `core/model/Project.h` владеют проектом, выбором дорожки, транспортом и редактированием. `App::edit` записывает историю, обновляет контекст аранжировки и публикует снимки. `History` объединяет жест в одну правку. UNDO/REDO восстанавливают контекст редактирования, на который указывает запись истории.
- `AppNative.kt` ↔ `jni_app.cpp` и тематические `jni_*.cpp` передают действия и массивы данных. `jni_common.h` даёт общий доступ к `App` и мьютексу. Пары `AppNative.Act` / `Act` и кодеки вроде `GridData.kt` / `ArrangeData.kt` должны изменяться синхронно.
- `ui/ScreenState.kt` выполняет `App::tick` на кадре. Структурные данные читаются при смене ревизии, живые показатели считываются отдельно. Compose хранит открытые меню, состояния жестов и положение окна.
- `AppProjects.cpp`, `ProjectStore` и `ProjectSession` хранят проекты в `filesDir/Projects` в JSON-формате Swift. `ProjectPack` обрабатывает обмен `.jambo` с медиа. Настройки устройства находятся в SharedPreferences `jambo`, `jambo-midi` и `jambo-sync`. Дополнительные настройки передаются через `ArrangePreferences.kt` → `AppAdditionalSettings.cpp`.

## Аудиодвижок

- `audio_engine.cpp` запрашивает у Oboe стереопоток float 48 кГц с низкой задержкой и эксклюзивным доступом. Фактический API и буфер определяет Android, после ошибки поток может переоткрыться. `Native.kt` ↔ `jni_bridge.cpp` обслуживают поток и телеметрию.
- `core/engine/AudioCore.cpp` принимает `SeqSnapshot` и `SoundSnapshot` по SPSC-очередям. Снимки создаются до колбэка, принимаются перед командами. Старые снимки возвращаются стороне управления на освобождение.
- `Sequencer` работает по кадрам аудио, обрабатывает шаги и p-lock. `Arpeggiator`, `core/mod/`, `core/sampler/` и `core/synth/` формируют голоса, которые затем идут в микшер. В Oboe планарные каналы ядра преобразуются в чередующиеся. [Задержка и диагностика Oboe](docs/ai/audio-engine.md). [Сверка DSP с эталонами Swift](docs/ai/core-port.md).

## MIDI

`MidiHub.kt` обнаруживает Android MIDI-устройства и маршрутизирует сообщения по устройству и порту. `BleMidiPeripheral.kt` обслуживает режим ADVERTISE. `MidiControls.kt` ведёт LEARN и назначения, передаёт ноты и контроллеры через `jni_midi.cpp` в `AppMidi.cpp` и `AppMidiTargets.cpp`. Сохранённые ID целей определяют `MidiTarget.kt` и `core/midi/MidiTarget.h`. [Подключение и особенности Android](docs/ai/midi.md).

## Синхронизация

- MIDI Clock включается в SYNC. `MidiSync.kt` выбирает активный источник и планирует отправку по портам. `AppMidi.cpp` и `core/midi/` вычисляют темп и фазу, связывают часы MIDI с кадрами `PlayClock` и учитывают задержку выхода.
- Ableton Link проходит отдельным путём через `link_session.cpp`, `jni_bridge.cpp` и меню `ControllerMenus.kt`. Меню показывает пиров, темп и фазу Link. В текущем пути `AudioEngine → AudioCore` не найдено подключения Link к рабочему транспорту грувбокса. Результаты старого спайка с Drambo описаны отдельно. [Заметки о Link](docs/ai/link.md).

## Микшер и мастер

`AudioCore::renderMix` собирает голоса из `Sampler`, обрабатывает мастер и мониторный выход. Маршрутизация эффектов, мастер-цепочка и EQ находятся в `core/fx/`, `core/master/`, `core/eq/`. `AppMix`, `AppMeter`, `AppMaster` и `AppMasterWin` обслуживают уровни, M/S, пресеты, анализ и запись. `Levels`, `StemRing` и `MeterState` разделяют сбор звука в колбэке и показания для Compose. Точки входа интерфейса — `jni_master*.cpp`, `MixerWindow.kt`, `MixCells.kt`, `MasterWindow.kt`, `EqView.kt`.

## Клип-лаунчер PATTERNS

Клипы и строки принадлежат `Project`, `Track`, `Clip` и `Clips.cpp`. `AppGrid.cpp` связывает сетку с запуском. При игре `core/sequencer/Launcher.cpp` ведёт активные и ожидающие клипы, переключает строки и сообщает позиции стороне управления через `SeqOut`. `PatternGrid.kt`, `PatternTouches.kt` и `GridSelection.kt` ведут жесты и рамку. `PatternGroupGhost.kt` рисует подвижную группу одним слоем, координата пальца читается при рисовании. При переносе через кнопку ARRANGE состояние жеста передаётся в `ScreenState.arrangeCarry`.

## Аранжировщик ARRANGE

- `core/model/ArrangeEdits.cpp`, `ArrangeAuto.cpp`, `ArrangeRecorder.cpp` и `GridSelection.cpp` отвечают за блоки, кривые, запись и перенос. `app/AppArrange*.cpp` и `jni_arrange.cpp` связывают модель с транспортом, REF, меню и предпросмотром вставки.
- `ArrangeWindow.kt`, `ArrangeViewport.kt`, `ArrangeHeaders.kt` и `ArrangeAutoPanel.kt` ведут экран, общую геометрию дорожек, жесты и AUTO. `Launcher` при игре по таймлайну ищет блок для позиции курсора. `ArrPattern` разделяет память одинаковых копий клипа до первой правки, после которой блок редактируется независимо.
- `AppSongExport.cpp` готовит снимок проекта. `core/engine/SongRender.cpp` рендерит его вне живого транспорта, а `jni_song_export.cpp` и `song_encode.cpp` кодируют файл. `SongExport.kt` ведёт экспорт и системное сохранение.
