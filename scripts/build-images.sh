#!/usr/bin/env bash
set -euo pipefail

OWNER="${OWNER:?set OWNER to your GHCR namespace, e.g. export OWNER=josh}"
GHCR_TOKEN="${GHCR_TOKEN:-}"
GHCR_USER="${GHCR_USER:-$OWNER}"
GNURADIO4_REPO="${GNURADIO4_REPO:-https://github.com/fair-acc/gnuradio4.git}"
GNURADIO4_REF="${GNURADIO4_REF:-main}"
CONTROL_PLANE_REF="${CONTROL_PLANE_REF:-$(git rev-parse HEAD)}"
CONTROL_PLANE_VERSION="${CONTROL_PLANE_VERSION:-$(git rev-parse --short HEAD)}"

BASE_IMAGE="ghcr.io/${OWNER}/gnuradio4-sdk"
SDK_IMAGE="ghcr.io/${OWNER}/gr4-control-plane-sdk"
RUNTIME_IMAGE="ghcr.io/${OWNER}/gr4-control-plane-runtime"

build_base() {
  local platform="$1"
  local tag="$2"

  docker buildx build \
    --platform "${platform}" \
    --target gnuradio4-sdk \
    -t "${BASE_IMAGE}:${tag}" \
    --build-arg GNURADIO4_REPO="${GNURADIO4_REPO}" \
    --build-arg GNURADIO4_REF="${GNURADIO4_REF}" \
    --build-arg GR_SPLIT_BLOCK_INSTANTIATIONS=OFF \
    --build-arg OCI_SOURCE=https://github.com/fair-acc/gnuradio4 \
    --build-arg OCI_URL=https://github.com/fair-acc/gnuradio4 \
    --build-arg OCI_REVISION="${GNURADIO4_REF}" \
    --build-arg OCI_VERSION="${GNURADIO4_REF}" \
    --push \
    .
}

docker buildx create --name gr4-ci --use >/dev/null 2>&1 || docker buildx use gr4-ci
docker buildx inspect --bootstrap >/dev/null

if [[ -n "${GHCR_TOKEN}" ]]; then
  printf '%s' "${GHCR_TOKEN}" | docker login ghcr.io -u "${GHCR_USER}" --password-stdin
fi

echo "Building GNU Radio 4 base image for amd64..."
build_base linux/amd64 amd64

if [[ "${PUBLISH_MULTIARCH:-0}" == "1" ]]; then
  echo "Building GNU Radio 4 base image for arm64..."
  build_base linux/arm64 arm64

  echo "Creating GNU Radio 4 base manifest..."
  docker buildx imagetools create \
    --tag "${BASE_IMAGE}:latest" \
    "${BASE_IMAGE}:amd64" \
    "${BASE_IMAGE}:arm64"
else
  docker buildx imagetools create \
    --tag "${BASE_IMAGE}:latest" \
    "${BASE_IMAGE}:amd64"
fi

echo "Building control-plane SDK image..."
docker buildx build \
  --platform linux/amd64 \
  --target sdk \
  -t "${SDK_IMAGE}:latest" \
  --build-arg GNURADIO4_SDK_IMAGE="${BASE_IMAGE}:latest" \
  --build-arg OCI_SOURCE="https://github.com/${OWNER}/gr4-control-plane" \
  --build-arg OCI_URL="https://github.com/${OWNER}/gr4-control-plane" \
  --build-arg OCI_REVISION="${CONTROL_PLANE_REF}" \
  --build-arg OCI_VERSION="${CONTROL_PLANE_VERSION}" \
  --push \
  .

echo "Building control-plane runtime image..."
docker buildx build \
  --platform linux/amd64 \
  --target runtime \
  -t "${RUNTIME_IMAGE}:latest" \
  --build-arg GNURADIO4_SDK_IMAGE="${BASE_IMAGE}:latest" \
  --build-arg OCI_SOURCE="https://github.com/${OWNER}/gr4-control-plane" \
  --build-arg OCI_URL="https://github.com/${OWNER}/gr4-control-plane" \
  --build-arg OCI_REVISION="${CONTROL_PLANE_REF}" \
  --build-arg OCI_VERSION="${CONTROL_PLANE_VERSION}" \
  --push \
  .
