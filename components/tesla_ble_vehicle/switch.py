import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
    CONF_RESTORE_MODE,
)

from . import TeslaBLEVehicle, SWITCHES

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_SWITCH_DEFS = {d["id"]: d for d in SWITCHES}


def _schema_for_type(type_id: str) -> cv.Schema:
    definition = _SWITCH_DEFS[type_id]

    return switch.switch_schema(
        definition["class"],
        icon=definition.get("icon"),
        default_restore_mode="RESTORE_DEFAULT_OFF",
    ).extend(
        {
            cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
            cv.Optional(CONF_NAME, default=definition["name"]): cv.string,
            cv.Optional(
                CONF_DISABLED_BY_DEFAULT, default=definition.get("disabled_by_default", False)
            ): cv.boolean,
            cv.Optional(
                CONF_RESTORE_MODE, default="RESTORE_DEFAULT_OFF"
            ): cv.enum(switch.RESTORE_MODES, upper=True),
        }
    )


CONFIG_SCHEMA = cv.typed_schema(
    {type_id: _schema_for_type(type_id) for type_id in _SWITCH_DEFS.keys()},
    key=CONF_TYPE,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    definition = _SWITCH_DEFS[config[CONF_TYPE]]

    sw = cg.new_Pvariable(config[CONF_ID])
    await switch.register_switch(sw, config)
    cg.add(sw.set_parent(parent))

    if definition.get("setter"):
        cg.add(getattr(parent, definition["setter"])(sw))
