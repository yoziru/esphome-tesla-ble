import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_ICON,
    CONF_NAME,
)

from . import TeslaBLEVehicle, BINARY_SENSORS, get_device_class_const

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_BINARY_SENSOR_DEFS = {d["id"]: d for d in BINARY_SENSORS}


def _apply_defaults(config):
    definition = _BINARY_SENSOR_DEFS[config[CONF_TYPE]]

    if CONF_NAME not in config:
        config[CONF_NAME] = definition["name"]

    if CONF_ICON not in config and "icon" in definition:
        config[CONF_ICON] = definition["icon"]

    if CONF_DEVICE_CLASS not in config and "device_class" in definition:
        dc = get_device_class_const(binary_sensor, definition["device_class"])
        if dc:
            config[CONF_DEVICE_CLASS] = dc

    return config


CONFIG_SCHEMA = cv.All(
    binary_sensor.binary_sensor_schema().extend(
        {
            cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
            cv.Required(CONF_TYPE): cv.one_of(*_BINARY_SENSOR_DEFS.keys(), lower=True),
        }
    ),
    _apply_defaults,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    sens = await binary_sensor.new_binary_sensor(config)
    cg.add(parent.set_binary_sensor(config[CONF_TYPE], sens))
