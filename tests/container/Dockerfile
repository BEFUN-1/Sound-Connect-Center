FROM docker.io/library/ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake curl ninja-build pkg-config python3 \
    libbluetooth-dev libdbus-1-dev libglfw3-dev libgl1-mesa-dev \
    qt6-base-dev qt6-declarative-dev \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-templates \
    qml6-module-qtquick-layouts qml6-module-qtquick-shapes qml6-module-qtquick-window \
    qml6-module-qtqml-workerscript \
    && rm -rf /var/lib/apt/lists/*

# Match tests/CMakeLists.txt and install Catch2 so test runs need no network.
RUN curl --fail --location --retry 3 \
        https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.8.1 \
        --output /tmp/catch2.tar.gz \
    && echo '18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79  /tmp/catch2.tar.gz' | sha256sum --check - \
    && mkdir /tmp/catch2 \
    && tar -xzf /tmp/catch2.tar.gz -C /tmp/catch2 --strip-components=1 \
    && cmake -S /tmp/catch2 -B /tmp/catch2-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/catch2 \
        -DBUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF \
    && cmake --build /tmp/catch2-build --parallel 2 \
    && cmake --install /tmp/catch2-build \
    && rm -rf /tmp/catch2 /tmp/catch2-build /tmp/catch2.tar.gz

ENV CMAKE_PREFIX_PATH=/opt/catch2 \
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

COPY run-tests.sh /usr/local/bin/sony-test
WORKDIR /src
ENTRYPOINT ["sh", "/usr/local/bin/sony-test"]
