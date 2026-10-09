# PlatformIO build verification

The GitHub Actions workflow `.github/workflows/platformio-build.yml` compiles the `esp32-c3-devkitm-1` environment using `pio run -e esp32-c3-devkitm-1`.

This workflow is a validation aid only. It does not change firmware behavior or merge changes into `main`.
