cmake_minimum_required(VERSION 3.22)
project(TeslaBLEComponentTests LANGUAGES C CXX)
if(NOT EXISTS "${TESLA_BLE_DIR}/include/vehicle.h")
  message(FATAL_ERROR "Set TESLA_BLE_DIR to the shared TeslaBLE checkout")
endif()
add_subdirectory("${TESLA_BLE_DIR}" tesla-ble EXCLUDE_FROM_ALL)
add_executable(test_command_results
  test_command_results.cpp
  host/platform.cpp
  ../components/tesla_ble_vehicle/tesla_ble_vehicle.cpp
  ../components/tesla_ble_vehicle/vehicle_state_manager.cpp)
target_compile_features(test_command_results PRIVATE cxx_std_23)
target_compile_options(test_command_results PRIVATE -Wall -Wextra)
set_source_files_properties(test_command_results.cpp PROPERTIES COMPILE_OPTIONS -fno-access-control)
target_include_directories(test_command_results PRIVATE host ../components/tesla_ble_vehicle)
target_link_libraries(test_command_results PRIVATE TeslaBLE)
