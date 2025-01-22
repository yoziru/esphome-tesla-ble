"""Tesla BLE Listener."""

import esphome.codegen as cg
from esphome.components import esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS: list[str] = ["@yoziru", "@Snuffy2"]
DEPENDENCIES: list[str] = ["esp32_ble_tracker"]
CONF_VIN = "vin"

tesla_ble_listener_ns = cg.esphome_ns.namespace("tesla_ble_listener")
TeslaBLEListener = tesla_ble_listener_ns.class_(
    "TeslaBLEListener", esp32_ble_tracker.ESPBTDeviceListener
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TeslaBLEListener),
        cv.Required(CONF_VIN): cv.string,
    }
).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)


def to_code(config):
    """Generate code."""
    var = cg.new_Pvariable(config[CONF_ID])
    yield esp32_ble_tracker.register_ble_device(var, config)

    cg.add(var.set_vin(config[CONF_VIN]))
