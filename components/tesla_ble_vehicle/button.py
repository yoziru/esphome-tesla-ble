import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
)

from . import TeslaBLEVehicle, BUTTONS

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"
CONF_TYPE = "type"

_BUTTON_DEFS = {d["id"]: d for d in BUTTONS}


def _schema_for_type(type_id: str) -> cv.Schema:
    definition = _BUTTON_DEFS[type_id]

    button_schema_kwargs = {"icon": definition.get("icon")}
    if definition.get("entity_category") == "diagnostic":
        button_schema_kwargs["entity_category"] = "diagnostic"

    schema = button.button_schema(
        definition["class"],
        **button_schema_kwargs,
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
    {type_id: _schema_for_type(type_id) for type_id in _BUTTON_DEFS.keys()},
    key=CONF_TYPE,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])
    definition = _BUTTON_DEFS[config[CONF_TYPE]]

    btn = cg.new_Pvariable(config[CONF_ID])
    await button.register_button(btn, config)
    cg.add(btn.set_parent(parent))

    if definition.get("setter"):
        cg.add(getattr(parent, definition["setter"])(btn))
