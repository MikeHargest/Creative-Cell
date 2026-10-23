# Creative Cell

Менеджер и просмотрщик 3D-ассетов: библиотека моделей с превью, быстрый
поиск и фильтры, 3D-инспектор. Одна нативная программа под Windows
(C++17, OpenGL, Dear ImGui, SQLite) — без сервера и браузера. Вьювер, камера
и навигационный кубик — общие с Expanse, другим проектом автора.

## Сборка

```
.tools\cmake\bin\cmake.exe -B build -S . -G "Visual Studio 17 2022" -A x64
.tools\cmake\bin\cmake.exe --build build --config Release
```

Нужен Visual Studio 2022 (C++). CMake лежит в `.tools/` (не в git; любой
CMake 3.20+ подойдёт). GLFW, GLM, Dear ImGui и FreeType подтягиваются при
первой конфигурации; SQLite, ufbx, cgltf, stb и шрифты — в `third_party/` и
`assets/`.

Запуск: `build/bin/Release/CreativeCell.exe`.
Самопроверка ядра без окна: `ctest --test-dir build -C Release`
(или `build/bin/Release/cc_selftest.exe`).

Ключи запуска (для проверок): `--data <папка>` — своя библиотека вместо
`%LOCALAPPDATA%\CreativeCell`; `--shot <файл.png> [--frames N]` — снимок
окна и выход; `--select N` — выделить N-й ассет; пути к файлам / папкам —
импортировать.

## Установщик

```
.tools\cmake\bin\cmake.exe --build build --config Release
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

Нужен NSIS 3 (`.tools\nsis`, PATH, Program Files или `-Makensis <путь>`).
Версия берётся из `CMakeLists.txt`. Результат — `dist\CreativeCell_Setup_<версия>.exe`:
папка установки, ярлыки в «Пуске» и на рабочем столе, интеграция с
проводником (миниатюры, «Открыть с помощью» — для всех пользователей),
деинсталлятор в «Приложениях». Библиотека пользователя
(`%LOCALAPPDATA%\CreativeCell`) при удалении остаётся.

## Как устроено

- **Библиотека** — `%LOCALAPPDATA%\CreativeCell\library.db` (SQLite) и
  превью `thumbs\<id>.png`. Файлы индексируются там, где лежат; копирование
  в папку библиотеки — опция импорта.
- **Ассеты** — FBX, OBJ, glTF, GLB, STL. Загрузка, подсчёт и превью — в
  фоне на нескольких потоках; сначала то, что на экране.

## Лицензия

Все права защищены. Код открыт для чтения, но использовать, собирать,
копировать, изменять и распространять его без письменного разрешения автора
нельзя — см. [LICENSE](LICENSE). Сторонние библиотеки и шрифты — под своими
лицензиями: [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).
