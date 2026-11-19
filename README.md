# Streamboard OBS

Native OBS Studio plugin for automatic Twitch and VK Play Live category updates
through [Streamboard](https://streamboard.sip1ay.pro).

**Supported:** Windows 11 x64, OBS Studio **33.0.1**. Other OBS 33.x builds are
allowed to load but have not been validated. macOS, Linux and ARM64 are outside
this release. Streamboard Companion remains available as a separate application.

## Установка

Скачайте файлы из [GitHub Releases](https://github.com/webvibecoding/streamboard-obs/releases/latest).

- `Streamboard.OBS-Setup-1.0.2.exe`: закройте OBS и запустите установщик.
  Стандартный каталог: `C:\ProgramData\obs-studio\plugins\streamboard-obs`.
- `Streamboard.OBS-1.0.2-win-x64.zip`: для portable OBS распакуйте в корень
  portable-копии. Должен получиться файл
  `OBS\plugins\streamboard-obs\streamboard-obs.dll` и каталог `data` рядом.
  Для обычного OBS ZIP можно распаковать в `C:\ProgramData\obs-studio`.

Это пакет плагина; установленный OBS уже должен присутствовать. Qt и libobs
берутся из OBS. Модуль HTTPS/TLS Schannel поставляется рядом с плагином.
При обновлении закройте OBS, замените файлы плагина и запустите
его снова. Удаление через «Установленные приложения» удаляет только плагин.
Portable-пакет удаляется вместе с папкой `plugins\streamboard-obs`.

Бинарные файлы пока не подписаны сертификатом. Windows SmartScreen может показать
предупреждение о неизвестном издателе. Сверьте загрузку с `SHA256SUMS.txt`:

```powershell
Get-FileHash .\Streamboard.OBS-Setup-1.0.2.exe -Algorithm SHA256
```

## Подключение

1. Откройте панель **Streamboard** через меню **Док-панели / Docks** в OBS.
2. Нажмите **Подключить Streamboard**.
3. В открывшемся браузере войдите в свой аккаунт и подтвердите код устройства.
4. Вернитесь в OBS. Плагин появится в Streamboard как отдельное устройство OBS.

Сопоставления категорий общие с Companion в том же аккаунте. Плагин использует
собственную сессию и запись Windows Credential Manager. Выберите один клиент
для автоматической синхронизации на компьютере: закройте Companion при переходе
на плагин. Одновременно активен один экземпляр плагина на пользователя.

## Использование

Пока OBS открыт, плагин каждые 2 секунды проверяет активное окно. Steam и Epic
определяются по локальным манифестам, остальные игры — по EXE или ручному имени.
После 10 секунд стабильного фокуса выполняется синхронизация. Пока игра активна,
эфиры повторно проверяются каждые 30 секунд. Запуск эфира в OBS ускоряет проверку
уже подтверждённой игры. Состояние эфира каждой платформы определяет сервер,
включая мультистрим через дополнительные выходы OBS.

При закрытии игры, фокусе на обычном приложении или паузе категория сохраняется.
Плагин меняет только категорию на подключённых платформах, где уже идёт эфир.
Заголовки, теги и остальные настройки не изменяются. Kick, Trovo и YouTube не
получают запросов обновления категории.

На вкладке **Сохранённые сопоставления** нажмите ячейку Twitch или VK, найдите
категорию и сохраните. Игру для этого запускать не требуется. Изменение вступит
в силу при следующей штатной синхронизации активной игры и живого эфира.
Для неизвестного EXE укажите название через **Выбрать категорию**.

Проверка обновлений выполняется не чаще раза в сутки и по кнопке. Плагин открывает
страницу релиза; установка обновления выполняется пользователем после закрытия OBS.

## Privacy and security

Only a storefront/hash game key, game name and executable basename are sent to
Streamboard. Window titles and full executable paths are never collected for
upload. Platform OAuth tokens remain on the server. See [PRIVACY.md](PRIVACY.md)
and [SECURITY.md](SECURITY.md).

## Build

Use Visual Studio 2026 C++ tools, Windows SDK 10.0.26100.0 and CMake 4.2+ with
the [Visual Studio 18 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html).
CMake verifies pinned SHA256 hashes for official
OBS 33.0.1 sources and obs-deps/Qt 2026-10-23, then builds the public OBS SDK.
The matching Qt Schannel TLS backend is installed under the plugin's private
`data/qt-plugins/tls` directory. Qt runtime libraries in OBS are left in place.
See [Qt deployment guidance](https://doc.qt.io/qt-6/ssl.html#considerations-while-packaging-your-application)
and the included Qt license/source notices.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel 4
$env:PATH = "$pwd/.deps/qt6-2026-10-23/bin;$pwd/.deps/sdk/bin/64bit;$env:PATH"
ctest --preset windows-x64
cmake --install build_x64 --config Release --prefix artifacts/package/plugins
```

Windows CI additionally opens an isolated portable OBS, verifies that the dock
loaded, completes an unauthenticated HTTPS request to the desktop bootstrap API
(expected HTTP 401), and requests graceful shutdown. It also installs, retries with a locked DLL,
and uninstalls in a temporary directory while an unrelated process named obs.exe runs.
Both smoke scripts refuse to run outside GitHub Actions.
Automated tests use mock HTTP and credential storage. They do not
need a Streamboard account or live streams.

Manual Windows 11 checks with real Twitch/VK streams, installer/uninstaller and
in-game overlays are **deferred**. CI does not establish real live-stream behavior
on an end-user Windows 11 machine. See [manual checklist](docs/MANUAL_TESTS.md).

Tag `v<version>` runs the complete pipeline and creates a draft release containing
installer, ZIP, SHA256 checksums and SPDX SBOM. Publish the draft after reviewing
the CI and release asset checksums. Signing hooks are disabled unless the
`WINDOWS_SIGNING_ENABLED` repository variable and signing secrets are configured.

## License

GPL-2.0-or-later; see [LICENSE](LICENSE). The Companion algorithms and SB artwork
are reused under MIT with their original notice retained in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
