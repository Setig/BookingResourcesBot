# Сборка Linux-пакетов

## Оглавление
- [Матрица пакетов](#матрица-пакетов)
- [Зависимости образа](#зависимости-образа)
- [Сборка](#сборка)
- [Результат](#результат)
- [Замечания по пакетам](#замечания-по-пакетам)

Linux-пакеты `.deb` для [`booking-resources-server`](applications.md#booking-resources-server), [`booking-resources-web`](applications.md#booking-resources-web) и [`booking-resources-cli`](applications.md#booking-resources-cli) собираются через Docker. Один параметризованный образ собирает одну пару Ubuntu и Qt, после чего CPack записывает четыре Debian-компонента проекта: `booking-resources-bot-common`, `booking-resources-bot-server`, `booking-resources-bot-web` и `booking-resources-bot-cli`.

Сборка в Docker настраивает CMake как Release-дерево с `BUILD_TESTING=ON`, собирает исходники, запускает единый исполняемый файл `tests` через `ctest` и только после этого вызывает `cpack -G DEB`. Падение тестов останавливает образ до создания пакетов. RPM-пакеты и runtime-образы для запуска процессов не создаются.

## Матрица пакетов

| Образ Ubuntu | Основная версия Qt | Типичные пакеты Qt в дистрибутиве |
| --- | --- | --- |
| `ubuntu:24.04` | 5 | Qt 5.15 |
| `ubuntu:24.04` | 6 | Qt 6.4 |
| `ubuntu:26.04` | 5 | Qt 5.15 |
| `ubuntu:26.04` | 6 | Qt 6.10 |

В каждом образе ставится только одна основная версия Qt. CMake предпочитает Qt 6, если найдены обе (`find_package(QT NAMES Qt6 Qt5 ...)`), поэтому смешанный toolchain молча выбрал бы Qt 6.

На момент добавления этого сценария Ubuntu 26.04 ещё публиковал пакеты разработки Qt 5, но Canonical обсуждала удаление Qt 5 из этого LTS. Если `qtbase5-dev` исчезнет, ячейка Qt 5 / 26.04 упадёт на `apt-get install`, а не соберётся против Qt 6.

## Зависимости образа

Общие пакеты: `build-essential`, `cmake`, `ninja-build`, `pkg-config`, `file`, `dpkg-dev`, `fakeroot`, `tzdata`.

Qt 5: `qtbase5-dev`, `qttools5-dev`, `libqt5sql5-sqlite`.

Qt 6: `qt6-base-dev`, `qt6-tools-dev`, `libqt6sql6-sqlite`. Пакет `qt6-tools-dev` нужен, чтобы CMake нашёл `LinguistTools`.

## Сборка

Должен быть запущен Docker Desktop (Windows) или Docker Engine (Linux). Из корня репозитория:

```powershell
.\docker\build.ps1
.\docker\build.ps1 -Ubuntu 24.04 -Qt 6
.\docker\build.ps1 -Ubuntu 24.04 -Qt 5
.\docker\build.ps1 -All
```

```sh
./docker/build.sh
./docker/build.sh --ubuntu 24.04 --qt 6
./docker/build.sh --ubuntu 24.04 --qt 5
./docker/build.sh --all
```

По умолчанию собирается Ubuntu 24.04 и Qt 6. `-All` / `--all` последовательно собирает все четыре ячейки матрицы.

Скрипты вызывают BuildKit `docker build --output` и выгружают только сгенерированные `.deb` из стадии export. Репозиторий копируется в образ и не монтируется с хоста.

## Результат

Каждая ячейка пишет в свой каталог, чтобы одинаковые имена Debian-файлов не перезаписывали друг друга:

```text
dist/ubuntu-24.04-qt5/
dist/ubuntu-24.04-qt6/
dist/ubuntu-26.04-qt5/
dist/ubuntu-26.04-qt6/
```

Типичные файлы:

```text
booking-resources-bot-common_1.0.0_amd64.deb
booking-resources-bot-server_1.0.0_amd64.deb
booking-resources-bot-web_1.0.0_amd64.deb
booking-resources-bot-cli_1.0.0_amd64.deb
```

Каталог `dist/` — сгенерированный выход и в git не входит.

## Замечания по пакетам

Пакеты server, web и CLI зависят от `booking-resources-bot-common`. CPack `SHLIBDEPS` дополнительно добавляет зависимости на разделяемые библиотеки Qt того дистрибутива, на котором шла сборка. Ставьте пакеты на совпадающий выпуск Ubuntu: сборка Qt 5 требует runtime Qt 5, сборка Qt 6 — runtime Qt 6.

Linux-пакеты устанавливают `booking-resources-bot-server.service` и `booking-resources-bot-web.service`. Рабочая конфигурация описана в [приложениях](applications.md).
