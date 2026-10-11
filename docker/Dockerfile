# syntax=docker/dockerfile:1

ARG UBUNTU_VERSION=24.04
FROM ubuntu:${UBUNTU_VERSION} AS deps

ARG QT_MAJOR=6
ENV DEBIAN_FRONTEND=noninteractive

RUN set -eu; \
    case "${QT_MAJOR}" in \
      5) qt_packages="qtbase5-dev qttools5-dev libqt5sql5-sqlite" ;; \
      6) qt_packages="qt6-base-dev qt6-tools-dev libqt6sql6-sqlite" ;; \
      *) echo "Unsupported QT_MAJOR=${QT_MAJOR} (expected 5 or 6)." >&2; exit 1 ;; \
    esac; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      ninja-build \
      pkg-config \
      file \
      dpkg-dev \
      fakeroot \
      tzdata \
      ${qt_packages}; \
    rm -rf /var/lib/apt/lists/*

FROM deps AS build

WORKDIR /src
COPY . /src

RUN cmake -S /src -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON \
    && cmake --build /build \
    && ctest --test-dir /build --output-on-failure -R '^tests$' \
    && mkdir -p /artifacts /cpack-output \
    && cpack -G DEB --config /build/CPackConfig.cmake -B /cpack-output \
    && find /cpack-output -maxdepth 1 -name '*.deb' -exec cp {} /artifacts/ \; \
    && test "$(find /artifacts -name '*.deb' | wc -l)" -ge 4

FROM scratch AS export
COPY --from=build /artifacts/ /
