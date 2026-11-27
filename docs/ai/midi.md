# MIDI (отличия от iOS)
- android.media.midi: USB и виртуальные устройства открываются сами; Bluetooth MIDI — поиск по сервису BLE-MIDI (SCAN BLUETOOTH) и `MidiManager.openBluetoothDevice`; разрешения BLUETOOTH_SCAN, BLUETOOTH_CONNECT (до Android 12 — геолокация).
- ADVERTISE: телефон/планшет сам виден как Bluetooth MIDI-устройство — GATT-сервер с сервисом BLE-MIDI, реклама до подключения; разрешение BLUETOOTH_ADVERTISE. Пакеты BLE-MIDI разбираются сами (заголовок и метки времени убираются). Проверено: iPad подключается из настроек Bluetooth MIDI своих приложений, ноты из Loopy Pro приходят.
- Названия нот — как в JAMbo: 48 = C3, 60 = C4.
- Устройство открывается один раз (повторное открытие давало двойные сообщения).
- TouchOSC (iPad) по ADVERTISE: нота 3 из примера TouchOSC — ~10 Hz, неслышна; рабочие ноты — 48 и выше.

## Меню MIDI и SYNC
- MIDI LEARN подсвечивает выбранную цель жёлтым. Маппинг хранится в SharedPreferences `jambo-midi`. Регуляторы следуют текущей странице ручек. Коды сохранённых целей общие для C++ и Kotlin, при добавлении новых целей прежние коды сохраняются.
- Назначения доступны для транспорта, клавиатуры, шагов, страниц, MIX/MST, дорожек и сетки PATTERNS. Цель ARP переключает режим ARPEGGIATOR. CHORD TYPE выбирает тип аккорда, стиль ударных или направление ARP в текущем режиме.
- В SYNC настраиваются Ableton Link и MIDI Clock, вход и выход Clock, START/STOP и смещение. Настройки Android хранятся в SharedPreferences `jambo-sync`.
