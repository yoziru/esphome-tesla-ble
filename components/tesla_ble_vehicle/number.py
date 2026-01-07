import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ICON,
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_NAME,
    CONF_STEP,
    CONF_UNIT_OF_MEASUREMENT,
)

from . import TeslaBLEVehicle, NUMBERS

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_NUMBER_DEFS = {d["id"]: d for d in NUMBERS}


def _schema_for_type(type_id: str) -> cv.Schema:
    definition = _NUMBER_DEFS[type_id]

    return number.number_schema(
        definition["class"],
        icon=definition.get("icon"),
        unit_of_measurement=definition.get("unit"),
    ).extend(
        {
            cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
            cv.Optional(CONF_NAME, default=definition["name"]): cv.string,

            # Allow overriding limits from YAML (useful when you set charging_amps_max)
            cv.Optional(CONF_MIN_VALUE, default=definition["min"]): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=definition["max"] if definition["max"] != "config" else 32): cv.float_,
            cv.Optional(CONF_STEP, default=definition["step"]): cv.float_,
        }
    )


CONFIG_SCHEMA = cv.typed_schema(
    {type_id: _schema_for_type(type_id) for type_id in _NUMBER_DEFS.keys()},
    key=CONF_TYPE,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    definition = _NUMBER_DEFS[config[CONF_TYPE]]

    num = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    cg.add(num.set_parent(parent))

    if definition.get("setter"):
        cg.add(getattr(parent, definition["setter"])(num))
