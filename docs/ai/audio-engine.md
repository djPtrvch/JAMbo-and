# Звук (отличия от iOS)
- Oboe → AAudio, 48 kHz, float, стерео, LowLatency + Exclusive; буфер = 2 burst. На Pixel 7: MMAP, burst 96, буфер 192 (4 ms), задержка выхода 8,2 ms, XRuns 0.
- Вместо AVAudioSourceNode — колбэк Oboe `onAudioReady`; при отключении выхода поток открывается заново (`onErrorAfterClose`).
- Задержка выхода — `calculateLatencyMillis` (метка времени потока); касание → звук считается по метке времени кадра щелчка.
- Касания приходят пачками по кадрам экрана (~12 ms на Pixel 7) — для ручек и пэдов нужен `requestUnbufferedDispatch`.
