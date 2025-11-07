# AGENTS.md

This file provides guidance for AI agents working on this project.

## Project Overview

**ESPHome Tesla BLE** is an ESPHome component that enables ESP32 devices to manage Tesla vehicle charging over Bluetooth Low Energy (BLE). It provides Home Assistant integration for controlling charging parameters and monitoring vehicle status.

### Key Features
- BLE key pairing with Tesla vehicles
- Vehicle wake commands
- Charging control (on/off, amps, limit percentage)
- Status monitoring (sleep state, locks, user presence, charging flap)
- BLE signal strength monitoring
- Smart polling with configurable intervals

### Primary Use Case
This project is designed to be used as an external component in ESPHome configurations, allowing users to control their Tesla vehicles through Home Assistant using an ESP32 device with BLE capabilities.

## Technical Stack

### Languages and Frameworks
- **Python**: ESPHome component configuration and validation (`components/tesla_ble_vehicle/__init__.py`)
- **C++**: Core component implementation (all `.cpp` and `.h` files in `components/`)
- **YAML**: ESPHome configuration files

### Key Dependencies
- **ESPHome**: 2025.10.3 (see `requirements.txt`)
- **ESP-IDF/Arduino**: ESP32 framework for BLE and hardware
- **yoziru/tesla-ble**: External library for Tesla BLE protocol

### Supported Hardware
- ESP32 with BLE support (ESP32, ESP32-C3, ESP32-C6, ESP32-S3)
- Tested boards:
  - M5Stack NanoC6 (primary)
  - M5Stack AtomS3
  - Generic ESP32 boards

## Project Structure

```
esphome-tesla-ble/
├── components/
│   ├── tesla_ble_vehicle/     # Main component (C++/Python)
│   │   ├── __init__.py        # ESPHome config schema & code generation
│   │   ├── tesla_ble_vehicle.{cpp,h}  # Main component class
│   │   ├── ble_manager.{cpp,h}        # BLE connection management
│   │   ├── command_manager.{cpp,h}     # Command execution
│   │   ├── message_handler.{cpp,h}     # Protocol message handling
│   │   ├── polling_manager.{cpp,h}     # Smart polling logic
│   │   ├── session_manager.{cpp,h}     # BLE session handling
│   │   └── vehicle_state_manager.{cpp,h}  # State tracking
│   └── tesla_ble_listener/    # BLE scanner for finding vehicle MAC
├── packages/                   # Reusable YAML configurations
│   ├── base.yml               # Base configuration
│   ├── board.yml              # Board-specific settings
│   ├── client.yml             # Main client config
│   ├── listener.yml           # BLE listener config
│   ├── project.yml            # Project metadata
│   └── external_components.yml # External component references
├── boards/                     # Board-specific YAML files
│   ├── m5stack-nanoc6.yml
│   ├── m5stack-atoms3.yml
│   └── esp32-generic.yml
├── docs/                       # Documentation images
├── tesla-ble-*.yml            # Top-level config files for different boards
├── tesla-ble.example.yml      # Example configuration
├── Makefile                    # Build automation
├── Makefile.common            # Common make targets
├── requirements.txt           # Python dependencies
└── README.md                  # User documentation
```

## Architecture

### Component Architecture
The `tesla_ble_vehicle` component follows a modular design:

1. **TeslaBLEVehicle** (main): Coordinates all managers and exposes ESPHome entities
2. **BLEManager**: Handles BLE connections and reconnection logic
3. **SessionManager**: Manages BLE sessions and key pairing
4. **CommandManager**: Queues and executes vehicle commands
5. **MessageHandler**: Parses and processes protocol messages
6. **PollingManager**: Implements smart polling intervals based on vehicle state
7. **VehicleStateManager**: Tracks and updates vehicle state

### ESPHome Integration
The component exposes:
- **Buttons**: Wake, Pair, Regenerate Key, Force Update
- **Switches**: Charging on/off
- **Numbers**: Charging amps, Charging limit
- **Binary Sensors**: Asleep, Locked, User present, Charging flap
- **Sensors**: BLE signal, Battery level, Charging state
- **Text Sensors**: Vehicle state information

### Polling Strategy
- **VCSEC polling**: Basic status (safe when vehicle is asleep) - default 10s
- **Infotainment awake**: Detailed data when awake but inactive - default 30s
- **Infotainment active**: Frequent updates when charging/active - default 10s
- **Sleep timeout**: 11-minute wake window before allowing vehicle to sleep

## Development Workflow

### Prerequisites
- Python 3.10+
- `uv` package manager (https://docs.astral.sh/uv/) - Used by the Makefile to run ESPHome without installing it globally
- ESP32 hardware for testing

**Note**: The `uv` tool is automatically used by the Makefile to run ESPHome commands. It provides isolated execution of ESPHome without requiring a virtual environment or global installation.

### Initial Setup
1. Clone the repository
2. Copy `secrets.yaml.example` to `secrets.yaml` and configure:
   - WiFi credentials (`wifi_ssid`, `wifi_password`)
   - Tesla vehicle details (`ble_mac_address`, `tesla_vin`)

### Build System
The project uses Make + ESPHome:

```bash
# Validate configuration
make validate-config BOARD=m5stack-nanoc6

# Compile firmware
make compile BOARD=m5stack-nanoc6

# Upload to device
make upload BOARD=m5stack-nanoc6

# View logs
make logs HOST_SUFFIX=-<device-id>

# Clean build artifacts
make clean
```

**Supported boards**: `m5stack-nanoc6`, `m5stack-atoms3`, `esp32-generic`

### Docker Alternative
```bash
make compile_docker BOARD=m5stack-nanoc6
```

## Making Changes

### Code Changes

#### Python (ESPHome Configuration)
- File: `components/tesla_ble_vehicle/__init__.py`
- Contains: Config schema, validation, code generation
- Follow ESPHome patterns for config validation
- Use `cv.config_validation` for schema definitions

#### C++ (Component Implementation)
- Files: `components/tesla_ble_vehicle/*.{cpp,h}`
- Follow ESPHome component lifecycle: `setup()`, `loop()`, `update()`
- Use ESPHome logging macros: `ESP_LOGD`, `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`
- Keep managers modular and focused on single responsibilities
- Handle BLE disconnections gracefully

#### YAML Configuration
- Package files in `packages/` are reusable configurations
- Board files in `boards/` contain hardware-specific settings
- Top-level `tesla-ble-*.yml` files combine packages for specific boards
- Dashboard variants (`*.dashboard.yml`) are for external ESPHome instances

### Testing Changes

#### Local Testing
1. Build the firmware: `make compile BOARD=<board>`
2. Flash to device: `make upload BOARD=<board>`
3. Monitor logs: `make logs`
4. Test in Home Assistant with real vehicle

#### Testing Guidelines
- Always test BLE reconnection scenarios
- Verify charging commands work correctly
- Check polling intervals don't drain vehicle battery
- Ensure vehicle can sleep when inactive
- Test with vehicle both near and far from ESP32

### Common Development Tasks

#### Adding a New Command
1. Add command enum to `common.h`
2. Implement command builder in `command_builder.h`
3. Add command execution in `command_manager.cpp`
4. Expose as button/switch/number in `__init__.py`
5. Update documentation

#### Adding a New Sensor
1. Add sensor to `__init__.py` configuration schema
2. Create text sensor/binary sensor/sensor in ESPHome
3. Update state in `vehicle_state_manager.cpp`
4. Publish value in `tesla_ble_vehicle.cpp`

#### Modifying Polling Logic
- Edit `polling_manager.{cpp,h}`
- Consider battery impact on vehicle
- Test with vehicle in different states (sleeping, awake, charging)

## Style Guidelines

### C++ Code Style
- Use ESPHome coding style (camelCase for methods, snake_case for variables)
- Include guards: `#pragma once`
- Log with appropriate level (DEBUG for verbose, INFO for important events)
- Use `optional<>` for values that may not be available

### Python Code Style
- Follow ESPHome conventions
- Use type hints where applicable
- Config validation should provide helpful error messages

### YAML Style
- Use 2-space indentation
- Keep substitutions at the top
- Comment non-obvious configuration choices

## Common Pitfalls

1. **BLE Disconnections**: Always handle connection loss gracefully
2. **Vehicle Sleep**: Respect the 11-minute wake window, don't prevent sleep
3. **Polling Too Fast**: Can drain vehicle 12V battery
4. **Missing Error Handling**: BLE and vehicle commands can fail
5. **State Sync**: Vehicle state may change outside ESPHome (Tesla app)

## Resources

### Documentation
- [ESPHome Documentation](https://esphome.io/)
- [Tesla BLE Protocol](https://github.com/teslamotors/vehicle-command)
- [Project README](README.md)

### External Components
- [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) - Tesla BLE library

### Hardware References
- [M5Stack NanoC6](https://docs.m5stack.com/en/core/M5NanoC6)
- [M5Stack AtomS3](https://docs.m5stack.com/en/core/AtomS3)

## Contributing Guidelines

When contributing to this project:

1. **Minimal Changes**: Make the smallest possible change to fix the issue
2. **Test Thoroughly**: Test with real hardware and vehicle
3. **Preserve Compatibility**: Don't break existing configurations
4. **Follow Patterns**: Match existing code style and architecture
5. **Document**: Update README.md if adding user-facing features
6. **ESPHome Version**: Keep compatible with version in `requirements.txt`

## Security Considerations

1. **Private Keys**: BLE keys are generated and stored on-device
2. **Secrets**: Never commit `secrets.yaml` (already in `.gitignore`)
3. **Network Security**: Use encrypted API keys for Home Assistant
4. **Vehicle Access**: Pairing requires physical NFC card tap

## For AI Agents

### Key Points
- This is an **ESPHome component**, not a standalone application
- Changes should maintain compatibility with ESPHome's architecture
- The component manages a **stateful BLE connection** with retry logic
- **Battery impact** on the vehicle is a primary concern
- Testing requires **real hardware** (ESP32 + Tesla vehicle)

### When Making Changes
- Always consider the impact on vehicle battery
- Preserve the modular architecture of managers
- Follow ESPHome's component lifecycle patterns
- Test BLE edge cases (disconnections, timeouts, retries)
- Keep polling intervals configurable
- Don't add dependencies unless absolutely necessary

### Build Verification
```bash
# Quick validation without hardware
make validate-config BOARD=m5stack-nanoc6

# Full build test
make compile BOARD=m5stack-nanoc6
```

### Questions to Ask
- Does this change affect vehicle battery life?
- Is this compatible with ESPHome's component model?
- Have I tested reconnection scenarios?
- Are error conditions handled properly?
- Is the configuration schema properly validated?
