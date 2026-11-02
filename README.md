<div align="center">
  <img src="WardogsGameBarCpp/GameBarWidget/Assets/logo.png" alt="WARDOGS Mortar Calculator" width="120" height="120" />
  <h1>WARDOGS Mortar Calculator</h1>
  <p><b>Координаты с карты → азимут, прицел и дистанция.</b></p>
  <p>Оверлей для миномёта в WARDOGS · Дальность 80–684 м · Русский / English</p>
  <p>
    <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=flat-square" alt="Windows 10 / 11" />
    <img src="https://img.shields.io/badge/C%2B%2B-20-38bdf8?style=flat-square" alt="C++20" />
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-fbbf24?style=flat-square" alt="MIT License" /></a>
  </p>
  <p><a href="#запуск">Запуск</a> · <a href="#управление">Управление</a> · <a href="WardogsGameBarCpp/docs/BUILD.md">Сборка</a></p>
</div>

---

## Запуск

| Xbox Game Bar | Portable |
| :--- | :--- |
| **`WardogsGameBar.msix`** | **`WardogsPortable.exe`** |
| Установите пакет → **Win + G** → выберите калькулятор в виджетах. Закрепите булавкой, чтобы он оставался поверх игры. | Запустите EXE без установки. Если появится проверка OCR — следуйте подсказке. |

## Управление

Наведите курсор на координаты орудия и нажмите **F1**, затем на координаты цели — **F2**. Результат появится в оверлее.

| Клавиша | Действие |
| --- | --- |
| **`F1`** | Позиция орудия |
| **`F2`** | Позиция цели |
| **`F3`** | Настройки |
| **`F4`** | Скрыть / показать |

В настройках — язык RU/EN, прозрачность, размер захвата, превью, горячие клавиши и ручной ввод X/Y. В Portable окно перемещается за заголовок в режиме F3. Сквозной режим пропускает клики в игру; в Game Bar он включается на самой панели.

Для выхода закройте виджет или нажмите F3 и крестик в Portable. **F4 только скрывает окно.**

## Что нужно

**Windows 10 2004+ / Windows 11 x64**, оконный режим или Borderless. Для распознавания нужен английский компонент **Optical Character Recognition** из параметров языка Windows.

Windows OCR может быть недоступен в Portable даже с установленным языком: Microsoft официально поддерживает его у приложений, установленных пакетом. В этом случае используйте Game Bar-версию или ручной ввод.

Калькулятор не вмешивается в игру и не сохраняет снимки экрана. На диске остаются только настройки. Правила использования сторонних инструментов определяют разработчики WARDOGS; гарантии от бана нет.

---

<div align="center">
  <p>Основано на <a href="https://github.com/ProDexortie/wardogs-mortar-calculator">проекте ProDexortie</a>.</p>
  <p>Copyright (c) 2026 ProDexortie · <a href="LICENSE">MIT License</a></p>
</div>
