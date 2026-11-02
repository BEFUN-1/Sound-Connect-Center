# Contributing

Bug reports, documentation improvements and hardware validation are welcome.
Read the [documentation index](docs/README.md) and [device matrix](docs/device-matrix.md)
before making compatibility claims.

## Build and validate

Use a C++20 compiler, CMake, matching Qt 6 development libraries and the platform
Bluetooth dependencies described in [packaging](packaging/README.md).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DSONY_REQUIRE_QT=ON -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --parallel
ctest --test-dir build --output-on-failure --no-tests=error
python -m unittest discover -s tests -p test_release_safety.py -v
git diff --check
```

For multi-configuration Windows generators, use `--config Release` when building
and `-C Release` with CTest. Catch2 can be fetched by CMake. Protocol and fake-device
tests do not require physical headphones.

Keep C++20 code in the existing transport, protocol, core or application module.
Use four-space indentation, PascalCase types and camelCase functions/locals.
Match existing QML formatting. Add focused behavior tests for protocol or
connection changes; describe the trigger and resulting behavior in the PR.

## Hardware reports

Include the model, firmware if returned by the device, OS, Bluetooth adapter,
exact steps, expected result and actual result. Distinguish protocol
implementation, simulation results and physical hardware observations. An ACK
alone does not prove a setting took effect or changed the sound.

Start with read-only checks on unverified devices. Only test write commands on
your own equipment with a clear recovery path. Restore any settings changed
during testing and describe what was verified.

## Privacy and repository contents

Before posting screenshots or logs, remove real Bluetooth addresses, paired
device/computer/phone names, account details, serial numbers, usernames, local
paths and credentials. Use synthetic devices and an isolated profile for
documentation screenshots; see [capture instructions](docs/screenshots/README.md).

Do not commit build output, installers, local QA logs, APKs, decompiled dumps,
certificates or user configuration. Publish reviewed binaries as Release assets.
The 2.0.6 stable download is the maintainer-verified existing installer, with
its unsigned status and original build provenance disclosed. Automated signed
releases use the separate provenance and signing checks in [release preparation](docs/release-preparation.md).
