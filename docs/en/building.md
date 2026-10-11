# Building Linux Packages

## Table of Contents
- [Package matrix](#package-matrix)
- [Image dependencies](#image-dependencies)
- [Build](#build)
- [Output](#output)
- [Package notes](#package-notes)

Linux `.deb` packages for [`booking-resources-server`](applications.md#booking-resources-server), [`booking-resources-web`](applications.md#booking-resources-web), and [`booking-resources-cli`](applications.md#booking-resources-cli) are produced with Docker. One parameterized image builds a single Ubuntu and Qt combination, then CPack writes the four Debian components already defined by the project: `booking-resources-bot-common`, `booking-resources-bot-server`, `booking-resources-bot-web`, and `booking-resources-bot-cli`.

The Docker build configures CMake as a Release tree with `BUILD_TESTING=ON`, builds the sources, runs the unified `tests` executable through `ctest`, and only then runs `cpack -G DEB`. A failing test run stops the image before packages are created. The build does not produce RPM packages or runtime images.

## Package matrix

| Ubuntu image | Qt major | Distro Qt packages (typical) |
| --- | --- | --- |
| `ubuntu:24.04` | 5 | Qt 5.15 |
| `ubuntu:24.04` | 6 | Qt 6.4 |
| `ubuntu:26.04` | 5 | Qt 5.15 |
| `ubuntu:26.04` | 6 | Qt 6.10 |

Each image installs only one Qt major version. CMake prefers Qt 6 when both are present (`find_package(QT NAMES Qt6 Qt5 ...)`), so a mixed toolchain would silently select Qt 6.

Ubuntu 26.04 still published Qt 5 development packages when this workflow was added, but Canonical has discussed removing Qt 5 from that LTS. If `qtbase5-dev` disappears, the Qt 5 / 26.04 cell fails at `apt-get install` instead of building against Qt 6.

## Image dependencies

Common packages: `build-essential`, `cmake`, `ninja-build`, `pkg-config`, `file`, `dpkg-dev`, `fakeroot`, `tzdata`.

Qt 5: `qtbase5-dev`, `qttools5-dev`, `libqt5sql5-sqlite`.

Qt 6: `qt6-base-dev`, `qt6-tools-dev`, `libqt6sql6-sqlite`. `qt6-tools-dev` is required so CMake can find `LinguistTools`.

## Build

Docker Desktop (Windows) or Docker Engine (Linux) must be running. From the repository root:

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

Default cell is Ubuntu 24.04 and Qt 6. `-All` / `--all` builds the four matrix cells sequentially.

The scripts invoke BuildKit `docker build --output` and copy only the generated `.deb` files out of the export stage. The repository is copied into the image; it is not bind-mounted from the host.

## Output

Each cell writes to its own directory so identical Debian file names do not overwrite each other:

```text
dist/ubuntu-24.04-qt5/
dist/ubuntu-24.04-qt6/
dist/ubuntu-26.04-qt5/
dist/ubuntu-26.04-qt6/
```

Typical files:

```text
booking-resources-bot-common_1.0.0_amd64.deb
booking-resources-bot-server_1.0.0_amd64.deb
booking-resources-bot-web_1.0.0_amd64.deb
booking-resources-bot-cli_1.0.0_amd64.deb
```

`dist/` is generated output and is not committed.

## Package notes

The server, web, and CLI packages depend on `booking-resources-bot-common`. CPack `SHLIBDEPS` also adds dependencies on the distro Qt shared libraries used by that cell. Install the packages on a matching Ubuntu release; a Qt 5 build needs the Qt 5 runtime, and a Qt 6 build needs the Qt 6 runtime.

Linux packages install `booking-resources-bot-server.service` and `booking-resources-bot-web.service`. Runtime configuration is documented in [Applications](applications.md).
