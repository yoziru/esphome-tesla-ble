import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, binary_sensor, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DOOR,
    DEVICE_CLASS_LOCK,
    DEVICE_CLASS_OCCUPANCY,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_KILOWATT,
)

CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["ble_client"]

tesla_ble_vehicle_ns = cg.esphome_ns.namespace("tesla_ble_vehicle")
TeslaBLEVehicle = tesla_ble_vehicle_ns.class_(
    "TeslaBLEVehicle", cg.PollingComponent, ble_client.BLEClientNode
)

AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]
CONF_VIN = "vin"
CONF_IS_ASLEEP = "is_asleep"
CONF_IS_UNLOCKED = "is_unlocked"
CONF_IS_USER_PRESENT = "is_user_present"
CONF_IS_CHARGE_FLAP_OPEN = "is_charge_flap_open"
CONF_CHARGE_POLLING_INTERVAL = "charge_polling_interval"
CONF_AWAKE_POLLING_INTERVAL = "awake_polling_interval"
CONF_BATTERY_LEVEL = "battery_level"
CONF_CHARGING_STATE = "charging_state"
CONF_CHARGER_POWER = "charger_power"
CONF_CHARGE_RATE = "charge_rate"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(TeslaBLEVehicle),
            # add support to set VIN (required)
            cv.Required(CONF_VIN): cv.string,
            cv.Optional(CONF_IS_ASLEEP): binary_sensor.binary_sensor_schema(
                icon="mdi:sleep"
            ).extend(),
            cv.Optional(CONF_IS_UNLOCKED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_LOCK,
            ).extend(),
            cv.Optional(CONF_IS_USER_PRESENT): binary_sensor.binary_sensor_schema(
                icon="mdi:account-check",
                device_class=DEVICE_CLASS_OCCUPANCY,
            ).extend(),
            cv.Optional(CONF_IS_CHARGE_FLAP_OPEN): binary_sensor.binary_sensor_schema(
                icon="mdi:ev-plug-tesla", device_class=DEVICE_CLASS_DOOR
            ).extend(),
            cv.Optional(
                CONF_CHARGE_POLLING_INTERVAL, default="10s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_AWAKE_POLLING_INTERVAL, default="5min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery",
            ).extend(),
            cv.Optional(CONF_CHARGING_STATE): text_sensor.text_sensor_schema(
                icon="mdi:ev-station"
            ).extend(),
            cv.Optional(CONF_CHARGER_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOWATT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:lightning-bolt",
            ).extend(),
            cv.Optional(CONF_CHARGE_RATE): sensor.sensor_schema(
                unit_of_measurement="mph",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:speedometer",
            ).extend(),
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
    cg.add(var.set_charge_polling_interval(config[CONF_CHARGE_POLLING_INTERVAL]))
    cg.add(var.set_awake_polling_interval(config[CONF_AWAKE_POLLING_INTERVAL]))

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

    if CONF_BATTERY_LEVEL in config:
        conf = config[CONF_BATTERY_LEVEL]
        s = await sensor.new_sensor(conf)
        cg.add(var.set_sensor_battery_level(s))

    if CONF_CHARGING_STATE in config:
        conf = config[CONF_CHARGING_STATE]
        s = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_text_sensor_charging_state(s))

    if CONF_CHARGER_POWER in config:
        conf = config[CONF_CHARGER_POWER]
        s = await sensor.new_sensor(conf)
        cg.add(var.set_sensor_charger_power(s))

    if CONF_CHARGE_RATE in config:
        conf = config[CONF_CHARGE_RATE]
        s = await sensor.new_sensor(conf)
        cg.add(var.set_sensor_charge_rate(s))
