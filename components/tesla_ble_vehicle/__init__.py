import esphome.codegen as cg
from esphome.components import binary_sensor, ble_client
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS: list[str] = ["@yoziru", "@Snuffy2"]
DEPENDENCIES: list[str] = ["ble_client"]

tesla_ble_vehicle_ns = cg.esphome_ns.namespace("tesla_ble_vehicle")
TeslaBLEVehicle = tesla_ble_vehicle_ns.class_(
    "TeslaBLEVehicle", cg.PollingComponent, ble_client.BLEClientNode
)

AUTO_LOAD: list[str] = ["binary_sensor"]
CONF_VIN = "vin"
CONF_IS_ASLEEP = "is_asleep"
CONF_IS_UNLOCKED = "is_unlocked"
CONF_IS_USER_PRESENT = "is_user_present"
CONF_IS_CHARGE_FLAP_OPEN = "is_charge_flap_open"

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(CONF_ID): cv.declare_id(TeslaBLEVehicle),
        # add support to set VIN (required)
        cv.Required(CONF_VIN): cv.string,
        cv.Optional(CONF_IS_ASLEEP): binary_sensor.binary_sensor_schema(icon="mdi:sleep").extend(),
        cv.Optional(CONF_IS_UNLOCKED): binary_sensor.binary_sensor_schema(
            device_class=binary_sensor.DEVICE_CLASS_LOCK,
        ).extend(),
        cv.Optional(CONF_IS_USER_PRESENT): binary_sensor.binary_sensor_schema(
            icon="mdi:account-check", device_class=binary_sensor.DEVICE_CLASS_OCCUPANCY
        ).extend(),
        cv.Optional(CONF_IS_CHARGE_FLAP_OPEN): binary_sensor.binary_sensor_schema(
            icon="mdi:ev-plug-tesla", device_class=binary_sensor.DEVICE_CLASS_DOOR
        ).extend(),
    })
    .extend(cv.polling_component_schema("1min"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await ble_client.register_ble_node(var, config)

    cg.add(var.set_vin(config[CONF_VIN]))

    if CONF_IS_ASLEEP in config:
        conf = config[CONF_IS_ASLEEP]
        bs = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_binary_sensor_is_asleep(bs))

    if CONF_IS_UNLOCKED in config:
        conf = config[CONF_IS_UNLOCKED]
        bs = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_binary_sensor_is_unlocked(bs))

    if CONF_IS_USER_PRESENT in config:
        conf = config[CONF_IS_USER_PRESENT]
        bs = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_binary_sensor_is_user_present(bs))

    if CONF_IS_CHARGE_FLAP_OPEN in config:
        conf = config[CONF_IS_CHARGE_FLAP_OPEN]
        bs = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_binary_sensor_is_charge_flap_open(bs))
