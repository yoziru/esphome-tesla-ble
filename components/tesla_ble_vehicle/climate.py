import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_DISABLED_BY_DEFAULT,
    CONF_ID,
    CONF_NAME,
)

from . import TeslaBLEVehicle, CLIMATE

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["tesla_ble_vehicle"]

CONF_TESLA_BLE_VEHICLE_ID = "tesla_ble_vehicle_id"

CONFIG_SCHEMA = climate.climate_schema(CLIMATE["class"]).extend(
    {
        cv.Required(CONF_TESLA_BLE_VEHICLE_ID): cv.use_id(TeslaBLEVehicle),
        cv.Optional(CONF_NAME, default=CLIMATE["name"]): cv.string,
        cv.Optional(CONF_DISABLED_BY_DEFAULT, default=False): cv.boolean,
        cv.Optional(CONF_ACCURACY_DECIMALS, default=1): cv.int_range(min=0, max=3),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TESLA_BLE_VEHICLE_ID])

    clm = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(clm, config)
    cg.add(clm.set_parent(parent))

    if CLIMATE.get("setter"):
        cg.add(getattr(parent, CLIMATE["setter"])(clm))
