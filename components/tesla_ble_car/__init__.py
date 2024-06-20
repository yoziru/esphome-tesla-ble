import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client
from esphome.const import (
    CONF_ID,
)

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["ble_client"]

tesla_ble_car_ns = cg.esphome_ns.namespace("tesla_ble_car")
TeslaBLECar = tesla_ble_car_ns.class_(
    "TeslaBLECar", ble_client.BLEClientNode
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(TeslaBLECar),
        }
    )
    # .extend(cv.polling_component_schema("5s"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await ble_client.register_ble_node(var, config)
