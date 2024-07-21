import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, binary_sensor
from esphome.const import (
    CONF_ID,
)

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["ble_client"]

tesla_ble_vehicle_ns = cg.esphome_ns.namespace("tesla_ble_vehicle")
TeslaBLEVehicle = tesla_ble_vehicle_ns.class_("TeslaBLEVehicle", cg.PollingComponent, ble_client.BLEClientNode)

AUTO_LOAD = ['binary_sensor']
CONF_VIN = "vin"
CONF_ASLEEP = "asleep"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(TeslaBLEVehicle),
            # add support to set VIN (required)
            cv.Required(CONF_VIN): cv.string,
            cv.Optional(CONF_ASLEEP): binary_sensor.binary_sensor_schema(icon="mdi:sleep").extend(),
        }
    )
    .extend(cv.polling_component_schema("1min"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await ble_client.register_ble_node(var, config)

    cg.add(var.set_vin(config[CONF_VIN]))

    if CONF_ASLEEP in config:
        conf = config[CONF_ASLEEP]
        bs = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_binary_sensor_asleep(bs))
