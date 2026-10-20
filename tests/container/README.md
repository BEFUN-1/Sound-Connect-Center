# Linux test container

This image builds all targets and runs the protocol, transport, core, Qt controller,
and simulated app tests. It uses Ubuntu 24.04 and the Qt/QML dependencies from CI.
Catch2 3.8.1 is installed with a checksum check when the image is built, so test
runs work without network access. Bluetooth hardware is not required.

Run these commands from the repository root:

```sh
git submodule update --init --recursive
docker build -t sony-device-center-tests -f tests/container/Dockerfile tests/container
docker run --rm --network=none \
    -v "$PWD:/src:ro" -v sony-device-center-build:/build \
    sony-device-center-tests
```

The source mount is read-only. The named volume keeps build files for later runs.
Use a separate volume for each checkout or build configuration.

For rootless Podman on Fedora, use:

```sh
podman build -t sony-device-center-tests -f tests/container/Dockerfile tests/container
podman run --rm --network=none --security-opt label=disable \
    -v "$PWD:/src:ro" -v sony-device-center-build:/build \
    sony-device-center-tests
```

`label=disable` lets the container read the checkout on SELinux systems without
relabeling source files. No host sockets or Bluetooth devices are mounted.

## Targeted tests and sanitizers

Arguments after the image name are passed to CTest. For example, append
`-R 'Speak-to-Chat|10-band|SonyDeviceDiscovery'` to run only matching tests.
Every run builds all targets first.

To build with ASan and UBSan, use a separate volume:

```sh
docker run --rm --network=none \
    -e ENABLE_SANITIZERS=ON -e BUILD_TYPE=Debug \
    -v "$PWD:/src:ro" -v sony-device-center-build-asan:/build \
    sony-device-center-tests
```

The default build parallelism is two jobs. Set `CMAKE_BUILD_PARALLEL_LEVEL` to
change it. The image runs Qt offscreen using software rendering and disables
ASan leak detection, matching the sanitizer settings in CI. Native Windows and
macOS builds and real Bluetooth behavior still need their respective environments.
