# Сборка

Нужны Visual Studio 2026, C++ tools **v145** и Windows SDK **10.0.26100.0**. Для Game Bar дополнительно установите **UWP tools**, **C++ Universal Windows Platform tools** и **MSIX Packaging Tools**.

Откройте [WardorsMortalCalc.slnx](../../WardorsMortalCalc.slnx), выберите **Release | x64**.

## Portable

ПКМ по **WardogsPortable → Собрать**. Готовый EXE:

```text
artifacts/x64/Release/Portable/WardogsPortable.exe
```

Для переноса нужен только EXE: CRT, логотип и значок включены в него. Настройки сохраняются рядом в `WardogsPortable.settings.json`, при отсутствии записи — в `%LOCALAPPDATA%\WardogsMortarCalculator`.

## Game Bar

1. Восстановите NuGet-пакеты решения — они попадут в корневую папку `packages/`.
2. Назначьте **WardogsGameBar** запускаемым проектом и соберите. **WardogsCapture** собирается автоматически и входит в пакет.
3. Для локального запуска: ПКМ по **WardogsGameBar → Развернуть**, затем **Win + G → Виджеты → WARDOGS Mortar Calculator**.

Для отдельного MSIX используйте **Publish → Create App Packages → Sideloading**, x64. Включите подпись пакета своим сертификатом: его Subject должен совпадать с Publisher `CN=WardogsMortarCalculator` в манифесте. Закрытый ключ в репозиторий не добавляйте.

```text
artifacts/x64/Release/MSIX/WardogsGameBar.msix
```

При распространении приложите необходимые зависимости из папки Dependencies, созданной Visual Studio. Microsoft Store для установки не нужен.

## Код

Расчёты находятся в `Core`, захват — в `Capture`, распознавание — в `OCR`, общие настройки — в `Settings`. Game Bar использует пакетированный Companion, Portable выполняет OCR внутри своего процесса. `CoreTests` — отдельный проект проверок расчётов.

У Windows OCR есть [ограничение на приложения без package identity](https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr). Portable проверяет доступность движка перед запуском, но ничего не устанавливает и не регистрирует. Для официально поддержанного варианта с Windows OCR используйте MSIX.
