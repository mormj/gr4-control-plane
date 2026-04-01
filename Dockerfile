# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04 AS base

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PKG_CONFIG=pkg-config

RUN apt-get update -q && apt-get install --no-install-recommends -qy \
    bash \
    build-essential \
    ca-certificates \
    ccache \
    cmake \
    file \
    git \
    gpg \
    lsb-release \
    locales \
    make \
    ninja-build \
    pkg-config \
    software-properties-common \
    wget \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update -q && apt-get install --no-install-recommends -qy \
    gcc-14 \
    g++-14 \
    libstdc++-14-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update -q && apt-get install --no-install-recommends -qy \
    cppzmq-dev \
    libbrotli-dev \
    libcurl4-openssl-dev \
    libcpp-httplib-dev \
    libfftw3-dev \
    libgtest-dev \
    libssl-dev \
    nlohmann-json3-dev \
    python3 \
    python3-dev \
    python3-numpy \
    pybind11-dev \
    libcli11-dev \
    librtaudio-dev libportaudio2 \
    libsoapysdr-dev soapysdr-module-hackrf soapysdr-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

FROM base AS gnuradio4-builder

ARG GNURADIO4_REPO=https://github.com/gnuradio/gnuradio4.git
ARG GNURADIO4_REF=main
ARG GR4_INCUBATOR_REPO=https://github.com/gnuradio/gr4-incubator.git
ARG GR4_INCUBATOR_REF=main

RUN git clone --depth 1 --branch "${GNURADIO4_REF}" "${GNURADIO4_REPO}" /opt/gnuradio4

WORKDIR /opt/gnuradio4

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithAssert \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_C_COMPILER=gcc-14 \
      -DCMAKE_CXX_COMPILER=g++-14 \
      -DGR_ENABLE_BLOCK_REGISTRY=ON \
      -DWARNINGS_AS_ERRORS=ON \
      -DTIMETRACE=OFF \
      -DADDRESS_SANITIZER=OFF \
      -DUB_SANITIZER=OFF \
      -DTHREAD_SANITIZER=OFF \
      -DBUILD_SHARED_LIBS=ON \
      -DGR4_USE_LIBCXX=OFF \
      -DENABLE_TESTING=OFF \
      -DENABLE_EXAMPLES=OFF \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build

RUN git clone --depth 1 --branch "${GR4_INCUBATOR_REF}" "${GR4_INCUBATOR_REPO}" /opt/gr4-incubator

WORKDIR /opt/gr4-incubator

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_C_COMPILER=gcc-14 \
      -DCMAKE_CXX_COMPILER=g++-14 \
      -DENABLE_TESTING=OFF \
      -DENABLE_EXAMPLES=OFF \
      -DENABLE_GUI_EXAMPLES=OFF \
      -DENABLE_PLUGINS=ON \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build

FROM base AS toolchain

COPY --from=gnuradio4-builder /usr/local /usr/local

RUN ldconfig

ENV GR4CP_GNURADIO4_PREFIX=/usr/local \
    GNURADIO4_PLUGIN_DIRECTORIES=/usr/local/lib

FROM toolchain AS builder

ARG GR4CP_INSTALL_PREFIX=/opt/gr4-control-plane

WORKDIR /workspaces/gr4-control-plane
COPY . .

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${GR4CP_INSTALL_PREFIX}" \
      -DCMAKE_C_COMPILER=gcc-14 \
      -DCMAKE_CXX_COMPILER=g++-14 \
      -DGR4CP_GNURADIO4_PREFIX=/usr/local \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build

FROM toolchain AS sdk

ARG OCI_SOURCE
ARG OCI_URL
ARG OCI_REVISION
ARG OCI_VERSION

COPY --from=builder /opt/gr4-control-plane /opt/gr4-control-plane

LABEL org.opencontainers.image.title="gr4-control-plane-sdk" \
      org.opencontainers.image.description="GR4 control-plane SDK image for building compatible downstream plugins" \
      org.opencontainers.image.source="${OCI_SOURCE}" \
      org.opencontainers.image.url="${OCI_URL}" \
      org.opencontainers.image.revision="${OCI_REVISION}" \
      org.opencontainers.image.version="${OCI_VERSION}"

ENV PATH="/opt/gr4-control-plane/bin:${PATH}" \
    GNURADIO4_PLUGIN_DIRECTORIES="/usr/local/lib:/opt/gr4-control-plane/lib"

WORKDIR /workspace
CMD ["/bin/bash"]

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PKG_CONFIG=pkg-config

RUN apt-get update -q && apt-get install --no-install-recommends -qy \
    ca-certificates \
    locales \
    libbrotli1 \
    libcurl4 \
    libjack-jackd2-0 \
    libportaudio2 \
    librtaudio6 \
    libssl3 \
    libsoapysdr0.8 \
    libzmq5 \
    soapysdr-module-all \
    && rm -rf /var/lib/apt/lists/*

COPY --from=toolchain /usr/local /usr/local

ARG OCI_SOURCE
ARG OCI_URL
ARG OCI_REVISION
ARG OCI_VERSION

COPY --from=builder /opt/gr4-control-plane /opt/gr4-control-plane

RUN ldconfig

LABEL org.opencontainers.image.title="gr4-control-plane-runtime" \
      org.opencontainers.image.description="Lean runtime image for gr4-control-plane" \
      org.opencontainers.image.source="${OCI_SOURCE}" \
      org.opencontainers.image.url="${OCI_URL}" \
      org.opencontainers.image.revision="${OCI_REVISION}" \
      org.opencontainers.image.version="${OCI_VERSION}"

ENV PATH="/opt/gr4-control-plane/bin:${PATH}" \
    GNURADIO4_PLUGIN_DIRECTORIES="/usr/local/lib:/opt/gr4-control-plane/lib"

WORKDIR /opt/gr4-control-plane
EXPOSE 8080
ENTRYPOINT ["/opt/gr4-control-plane/bin/gr4cp_server"]
