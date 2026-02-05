# Archive Manifest: Internet Radio v2 (ESP32-S3)

This project has been prepared for long-term archival. To ensure the code remains buildable, critical components have been vendored and patched to maintain compatibility with modern ESP-IDF versions.

## Build Environment

| Requirement | Version | Path |
| :--- | :--- | :--- |
| **ESP-IDF** | v5.5.1 | `/home/agkeck/esp/v5.5.1/esp-idf` |
| **ESP-ADF** | v2.7 (Release) | `/home/agkeck/esp/esp-idf/tools/esp-adf` |
| **Toolchain** | xtensa-esp-elf-14.2.0 | Standard IDF v5.5.1 Tools |
| **Target Chip** | ESP32-S3 | |

## Critical Modifications for Archival

### Vendored Components

The following components were moved from the global ADF directory into the project's `components/` directory to ensure they are captured in the project's source control/archive:

1. **esp_peripherals**: Vendored from `$ADF_PATH/components/esp_peripherals`.

### Compatibility Patches

To resolve a "fatal error: esp32/rom/crc.h: No such file or directory" issue caused by IDF v5.x removal of legacy headers on the S3 target:

* **File**: `components/esp_peripherals/lib/blufi/blufi_security.c`
* **Change**:
  * Replaced `#include "esp32/rom/crc.h"` with `#include "esp_rom_crc.h"`.
  * Replaced call to `crc16_be()` with `esp_rom_crc16_be()`.

## Verification Status

- [x] **Configuration**: Project configured for ESP32-S3.
* [x] **Compilation**: `idf.py build` completed successfully using the vendored components.
* [x] **Artifact Presence**: `build/internet_radio_v2.bin` generated.

## Recovery Notes

If this project needs to be built on a new system:

1. Install ESP-IDF v5.5.1 and its tools.
2. Install ESP-ADF v2.7.
3. Set `ADF_PATH` environment variable.
4. Run `idf.py build`. The project-local `esp_peripherals` will automatically override the version in the ADF directory.
