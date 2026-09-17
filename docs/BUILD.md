# Build

OpenGenesisLINK Server is C++23 and is designed for:

- Linux x86_64
- Linux ARM64/aarch64
- Windows x86_64, including Windows Server
- Windows ARM64 as a future optional target

Required on all platforms:

- CMake 3.25 or newer
- Ninja or another CMake generator
- a C++23 compiler
- OpenSSL / libcrypto

## Linux

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

Release build with warnings as errors:

```bash
cmake --preset release -DOGL_ENABLE_WERROR=ON
cmake --build --preset release
ctest --preset release
```

Full process-level test:

```bash
./scripts/smoke-test.sh
```

## Windows / Windows Server

The official Windows target uses MSVC. The codebase keeps operating-system-specific socket behavior behind the OpenGenesisLINK platform layer.

Example from an MSVC developer shell with vcpkg:

```powershell
vcpkg install openssl:x64-windows

cmake -S . -B build/windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DOGL_BUILD_TESTS=ON `
  -DOGL_ENABLE_WERROR=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build/windows
ctest --test-dir build/windows --output-on-failure
```

The GitHub Windows CI uses the same MSVC + vcpkg + OpenSSL path.

The Bash process smoke test is currently Linux-only. Windows CI compiles the Core, World Node and test binaries and runs the CTest suite. A native PowerShell process smoke will be added before Windows is called production-ready.
