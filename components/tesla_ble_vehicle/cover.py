import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_DISABLED_BY_DEFAULT,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
)

from . import TeslaBLEVehicle, COVERS

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_COVER_DEFS = {d["id"]: d for d in COVERS}


def _schema_for_type(type_id: str) -> cv.Schema:
    definition = _COVER_DEFS[type_id]

    schema = cover.cover_schema(
        definition["class"],
        icon=definition.get("icon"),
        device_class=definition.get("device_class"),
    ).extend(
        {
            cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
            cv.Optional(CONF_NAME, default=definition["name"]): cv.string,
            cv.Optional(
                CONF_DISABLED_BY_DEFAULT, default=definition.get("disabled_by_default", False)
            ): cv.boolean,
        }
    )

    return schema


CONFIG_SCHEMA = cv.typed_schema(
    {type_id: _schema_for_type(type_id) for type_id in _COVER_DEFS.keys()},
    key=CONF_TYPE,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    definition = _COVER_DEFS[config[CONF_TYPE]]

    cvr = cg.new_Pvariable(config[CONF_ID])
    await cover.register_cover(cvr, config)
    cg.add(cvr.set_parent(parent))

    if definition.get("setter"):
        cg.add(getattr(parent, definition["setter"])(cvr))
