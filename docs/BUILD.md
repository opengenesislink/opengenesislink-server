# Build

OpenGenesisLINK Server requires Linux, CMake 3.25+, Ninja and a C++23 compiler.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For Release:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Use `scripts/smoke-test.sh` for the Core restart/reconnect integration test.
