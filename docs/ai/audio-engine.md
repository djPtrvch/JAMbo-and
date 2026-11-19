# Звук (отличия от iOS)
- Oboe → AAudio, 48 kHz, float, стерео, LowLatency + Exclusive; буфер = 2 burst. На Pixel 7: MMAP, burst 96, буфер 192 (4 ms), задержка выхода 8,2 ms, XRuns 0.
- Вместо AVAudioSourceNode — колбэк Oboe `onAudioReady`; при отключении выхода поток открывается заново (`onErrorAfterClose`).
- Задержка выхода — `calculateLatencyMillis` (метка времени потока); касание → звук считается по метке времени кадра щелчка.
- Касания приходят пачками по кадрам экрана (~12 ms на Pixel 7) — для ручек и пэдов нужен `requestUnbufferedDispatch`.
- Колбэк рендерит Core: `AudioCore::render` блоками ≤ 4096 кадров, планарно → чередование; события и снимки кладёт сторона UI/MIDI под своим мьютексом, колбэк их только забирает (без блокировок).
- Колбэк ядра на Pixel 7 без подсказки производительности уходил на медленные ядра (52 / 95 %, щелчки). Включены `AudioStream::setPerformanceHintEnabled(true)` (ADPF; в Oboe 1.9.3 — у потока, не у builder) и `oboe::LatencyTuner`: стало 17 / 38 %, буфер 192 кадра (4 ms), XRuns 0, задержка выхода 9,7 ms.
