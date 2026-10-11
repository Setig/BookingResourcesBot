#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Build Ubuntu .deb packages with Docker.

Usage:
  docker/build.sh [--ubuntu 24.04|26.04] [--qt 5|6]
  docker/build.sh --all
EOF
}

if ! command -v docker >/dev/null 2>&1; then
  echo "docker was not found in PATH." >&2
  exit 1
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "${script_dir}/.." && pwd)

ubuntu_version=24.04
qt_major=6
build_all=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --ubuntu)
      ubuntu_version=${2:-}
      shift 2
      ;;
    --qt)
      qt_major=${2:-}
      shift 2
      ;;
    --all)
      build_all=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

build_cell() {
  local version=$1
  local qt=$2
  local dest_rel="dist/ubuntu-${version}-qt${qt}"
  local dest="${repo_root}/${dest_rel}"

  case "${version}" in
    24.04|26.04) ;;
    *)
      echo "Unsupported Ubuntu version: ${version} (expected 24.04 or 26.04)." >&2
      exit 1
      ;;
  esac
  case "${qt}" in
    5|6) ;;
    *)
      echo "Unsupported Qt major version: ${qt} (expected 5 or 6)." >&2
      exit 1
      ;;
  esac

  rm -rf -- "${dest}"
  echo "Building Ubuntu ${version} / Qt${qt} -> ${dest_rel}"
  DOCKER_BUILDKIT=1 docker build \
    -f "${script_dir}/Dockerfile" \
    --build-arg "UBUNTU_VERSION=${version}" \
    --build-arg "QT_MAJOR=${qt}" \
    --target export \
    --output "type=local,dest=${dest}" \
    "${repo_root}"
}

if [ "${build_all}" -eq 1 ]; then
  for version in 24.04 26.04; do
    for qt in 5 6; do
      build_cell "${version}" "${qt}"
    done
  done
else
  build_cell "${ubuntu_version}" "${qt_major}"
fi
