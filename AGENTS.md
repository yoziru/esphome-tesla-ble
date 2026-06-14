# Build Commands
make compile [BOARD=<board>]              # Compile firmware
make compile_docker [BOARD=<board>]       # Compile using Docker
make validate-config [BOARD=<board>]      # Validate YAML without building
make upload [BOARD=<board>] [HOST_SUFFIX] # Flash firmware to device
make logs [HOST_SUFFIX]                   # View device logs
make clean                                # Remove .esphome build dir
make help                                 # Show all targets

# Architecture
ESPHome component for Tesla BLE vehicle control using ESP32 boards (M5Stack NanoC6, AtomS3, generic ESP32). Two custom components:
- tesla_ble_vehicle: Main vehicle control (sensors, switches, charging, climate, locks)
- tesla_ble_listener: BLE vehicle discovery (find MAC address by VIN)

External Tesla BLE library (yoziru/tesla-ble) provides low-level BLE protocol implementation.
Session keys stored in ESP NVS flash.

Structure:
- boards/: Board-specific YAML configs (use Makefile BOARD= param)
- packages/: Reusable YAML packages (base, client, common, project, listener)
- components/: C++ + Python codegen (tesla_ble_vehicle, tesla_ble_listener)
- docs/: Screenshots for README

Install methods: ESPHome Dashboard (paste dashboard.yml URL), CLI with uv, or Docker.

# Code Style
Python: Imports grouped (esphome, then stdlib). snake_case functions/vars, PascalCase classes.
  Data-driven entity creation: define lists of dicts, loop to create. cv.templatable() for automation values.

C++: #pragma once, 2-space indent, K&R braces. Member vars with trailing underscore (vin_, ble_adapter_).
  ESP_LOG* macros (CONFIG/I/D/V/W/E). Namespace: esphome::tesla_ble_vehicle.
  Smart pointers preferred. ESPHome types: sensor::Sensor*, switch_::Switch*.

YAML: snake_case substitutions. Modular packages. Secrets in secrets.yaml.

Error handling: Check pointers before use. Return bool for success/failure helpers.

# Key Conventions
- VCSEC is always safe to poll (low-power controller, doesn't wake vehicle)
- Sleep state is detected via VCSEC; infotainment uses NO_WAKE_SKIP when asleep
- Sensors defined in __init__.py SENSORS/BINARY_SENSORS/TEXT_SENSORS lists
- Sensor values published in vehicle_state_manager.cpp update methods
- Commands use TeslaBLEVehicle::send_command() with Command tracking
