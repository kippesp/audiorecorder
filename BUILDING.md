# Building

## Requirements

- macOS (tested with macOS 26 Tahoe)
- Xcode Command Line Tools (`xcode-select --install`) with C++23 support
- CMake

## Build

```
cmake -B build -S src
cmake --build build
```

The binary is placed at `build/ra`.
