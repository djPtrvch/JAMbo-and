# Ableton Link (отличия от iOS)
- Официальная библиотека C++ Link 4.1.1 (GPLv2), не LinkKit; Android поддерживается библиотекой (LINK_PLATFORM_LINUX).
- Нужны разрешения INTERNET, ACCESS_WIFI_STATE, CHANGE_WIFI_MULTICAST_STATE и WifiManager.MulticastLock, пока Link включён.
- Время в колбэке — по счётчику кадров через `HostTimeFilter` (часы в момент колбэка дают двойные доли при буфере 96 кадров) плюс задержка выхода.
- Проверено с Drambo (iPad): участники, темп в обе стороны, фаза щелчков совпадает.
