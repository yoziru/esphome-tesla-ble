import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    CONF_ICON,
    CONF_NAME,
)

from . import TeslaBLEVehicle, TEXT_SENSORS

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_TEXT_SENSOR_DEFS = {d["id"]: d for d in TEXT_SENSORS}


def _apply_defaults(config):
    definition = _TEXT_SENSOR_DEFS[config[CONF_TYPE]]

    if CONF_NAME not in config:
        config[CONF_NAME] = definition["name"]

    if CONF_ICON not in config and "icon" in definition:
        config[CONF_ICON] = definition["icon"]

    return config


CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema().extend(
        {
            cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
            cv.Required(CONF_TYPE): cv.one_of(*_TEXT_SENSOR_DEFS.keys(), lower=True),
        }
    ),
    _apply_defaults,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    sens = await text_sensor.new_text_sensor(config)
    cg.add(parent.set_text_sensor(config[CONF_TYPE], sens))
