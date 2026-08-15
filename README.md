# VINOX

> **Versatile Inference & Native OpenVINO eXecution**  
> Standalone C++ GenAI infrastructure powered by OpenVINO.

## Overview

VINOX is a high-performance C++20 infrastructure for OpenVINO and OpenVINO GenAI, providing unified inference across:
- `vinox_core.dll` & `vinox_openvino.dll` (Clean C-ABI & C++20 wrapper)
- `vinox-cli.exe` (Interactive terminal app)
- `vinox-server.exe` (OpenAI-compatible HTTP/SSE server)
- `vinox-gui.exe` (Native Qt Desktop GUI)

## Build

Prerequisites on Windows x64:
- Visual Studio 2022 / Build Tools 2022 (x64 C++ toolchain)
- CMake 3.29+ (`C:\Program Files\CMake`)
- Ninja (`C:\Qt\Tools\Ninja`)

Build with CMake Presets:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --test-dir out/windows-msvc-debug/build --output-on-failure
```

## First Inference

```powershell
.\out\windows-msvc-debug\stage\bin\vinox-cli.exe `
    --model C:\path\to\openvino-model `
    --prompt "Explain VINOX in three sentences." `
    --device CPU `
    --max-new-tokens 64 `
    --temperature 0.7
```

## License

Apache License 2.0. See `LICENSE` and `NOTICE`.
