import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
)

from . import TeslaBLEVehicle, LOCKS

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_LOCK_DEFS = {d["id"]: d for d in LOCKS}


def _schema_for_type(type_id: str) -> cv.Schema:
    definition = _LOCK_DEFS[type_id]

    return lock.lock_schema(
        definition["class"],
        icon=definition.get("icon"),
    ).extend(
        {
            cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
            cv.Optional(CONF_NAME, default=definition["name"]): cv.string,
            cv.Optional(
                CONF_DISABLED_BY_DEFAULT, default=definition.get("disabled_by_default", False)
            ): cv.boolean,
        }
    )


CONFIG_SCHEMA = cv.typed_schema(
    {type_id: _schema_for_type(type_id) for type_id in _LOCK_DEFS.keys()},
    key=CONF_TYPE,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    definition = _LOCK_DEFS[config[CONF_TYPE]]

    lck = cg.new_Pvariable(config[CONF_ID])
    await lock.register_lock(lck, config)
    cg.add(lck.set_parent(parent))

    if definition.get("setter"):
        cg.add(getattr(parent, definition["setter"])(lck))
