# syntax=docker/dockerfile:1

# =====================================================================
# build 스테이지 — vcpkg 로 asio/protobuf/gtest 를 리눅스로 재빌드 후 CMake 빌드
# =====================================================================
FROM debian:bookworm-slim AS build

# vcpkg + CMake(Ninja) 빌드에 필요한 도구.
#  - protobuf 소스 빌드가 pkg-config/autoconf/libtool 을 요구한다.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        zip \
        unzip \
        tar \
        pkg-config \
        autoconf \
        automake \
        libtool \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# vcpkg 설치 (manifest 모드로 사용 — 버전은 baseline 에 고정).
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
ENV PATH="$VCPKG_ROOT:$PATH"

WORKDIR /src

# 1) 의존성 매니페스트만 먼저 복사 → 소스만 바뀌면 vcpkg 레이어를 캐시 재사용.
COPY vcpkg.json ./
# manifest 를 미리 설치해 두면 이후 configure 가 빠르다.
RUN vcpkg install --triplet x64-linux

# 2) 나머지 소스 복사 후 빌드.
COPY . .
RUN cmake --preset linux-gcc \
    && cmake --build build --config Release

# =====================================================================
# runtime 스테이지 — 바이너리만 담은 경량 이미지, non-root 실행
# =====================================================================
FROM debian:bookworm-slim AS runtime

# protobuf/asio 는 x64-linux(정적) 트리플렛으로 정적 링크된다. 남는 동적
# 의존성은 libstdc++/libgcc/libc 뿐 → 이것만 보장.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# 권한 최소화 — 비루트 유저로 실행.
RUN useradd --system --create-home --shell /usr/sbin/nologin appuser
USER appuser
WORKDIR /home/appuser

# 빌드 산출물 복사 (Ninja single-config → build/src/game_server/game_server).
#   게임 수직 슬라이스 셸(로그인→월드입장→이동). 설정(game_server.cfg)이 없으면
#   전부 기본값으로 뜨고 계정 0개 → 로그인은 거부되나 서버는 정상 리슨한다.
#   데모 계정/보안정책은 런타임에 game_server.cfg 볼륨 마운트로 주입한다(비밀 미포함).
COPY --from=build /src/build/src/game_server/game_server /usr/local/bin/game_server

# 기본 포트 (game_server/main.cpp 기본값과 일치).
EXPOSE 7777

# argv[1] 로 포트를 받으므로 CMD 로 오버라이드 가능:
#   docker run ... game-server 9000
ENTRYPOINT ["game_server"]
CMD ["7777"]
