import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32_ble_tracker
from esphome.const import CONF_ID

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["esp32_ble_tracker"]

tesla_ble_listener_ns = cg.esphome_ns.namespace("tesla_ble_listener")
TeslaBLEListener = tesla_ble_listener_ns.class_(
    "TeslaBLEListener", esp32_ble_tracker.ESPBTDeviceListener
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TeslaBLEListener),
    }
).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield esp32_ble_tracker.register_ble_device(var, config)
