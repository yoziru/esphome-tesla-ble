# Host tests

Run `TESLA_BLE_DIR=/path/to/tesla-ble make test`. CI checks out the released
library; the default is the sibling checkout. The native component test needs
CMake and a C++23 compiler (Clang or GCC). Its first build fetches the library's
pinned nanopb/mbedtls dependencies; subsequent builds are incremental.

`test_command_results.cpp` compiles the complete production
`tesla_ble_vehicle.cpp` and `vehicle_state_manager.cpp` and links the real
TeslaBLE library, including generated protobuf types and state conversions.
`host/` supplies only the ESPHome entity/status/clock API and BLE/NVS boundaries.
It does not replace the component, state manager, or Vehicle implementation.

The fixture observes entity publications and real queued library commands.
Success/skipped/rejected outcomes are injected through those commands' actual
completion callbacks; disconnect failures come from the library itself. This
tests host handling, not vehicle authentication or protocol delivery. Incoming
status and charging/sentry observations enter through the callbacks registered
by production setup, exercising their state-publication and freshness wiring.
Access checks are disabled only for the test source, avoiding production test
exports or redeclaring production classes.

The deterministic clock explicitly runs the small host warning timer. ESPHome's
real scheduler, persistence, radio, GATT discovery, and device timing remain
firmware/device-test boundaries; the stubs do not claim to validate them.
`make validate-dashboard-config` retains the separate codegen/configuration gate.
