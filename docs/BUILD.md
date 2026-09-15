# Build

OpenGenesisLINK Server requires:

- Linux
- CMake 3.25 or newer
- Ninja
- a C++23 compiler
- pthreads
- OpenSSL development headers / libcrypto

Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build libssl-dev
```

Development build and tests:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release build and tests:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Full process-level test:

```bash
./scripts/smoke-test.sh
```
