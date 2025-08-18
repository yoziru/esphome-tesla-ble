import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, binary_sensor, button, switch, number, sensor, text_sensor
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_DISABLED_BY_DEFAULT,
    CONF_ENTITY_CATEGORY,
    CONF_FORCE_UPDATE,
    CONF_ICON,
    CONF_ID,
    CONF_MODE,
    CONF_NAME,
    CONF_RESTORE_MODE,
    CONF_UNIT_OF_MEASUREMENT,
)
from esphome import automation


CODEOWNERS = ["@yoziru"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["binary_sensor", "button", "switch", "number", "sensor", "text_sensor"]

tesla_ble_vehicle_ns = cg.esphome_ns.namespace("tesla_ble_vehicle")
TeslaBLEVehicle = tesla_ble_vehicle_ns.class_(
    "TeslaBLEVehicle", cg.PollingComponent, ble_client.BLEClientNode
)

# Custom button classes
TeslaWakeButton = tesla_ble_vehicle_ns.class_("TeslaWakeButton", button.Button)
TeslaPairButton = tesla_ble_vehicle_ns.class_("TeslaPairButton", button.Button)
TeslaRegenerateKeyButton = tesla_ble_vehicle_ns.class_("TeslaRegenerateKeyButton", button.Button)
TeslaForceUpdateButton = tesla_ble_vehicle_ns.class_("TeslaForceUpdateButton", button.Button)

# Custom switch classes
TeslaChargingSwitch = tesla_ble_vehicle_ns.class_("TeslaChargingSwitch", switch.Switch)

# Custom number classes
TeslaChargingAmpsNumber = tesla_ble_vehicle_ns.class_("TeslaChargingAmpsNumber", number.Number)
TeslaChargingLimitNumber = tesla_ble_vehicle_ns.class_("TeslaChargingLimitNumber", number.Number)

# Actions
WakeAction = tesla_ble_vehicle_ns.class_("WakeAction", automation.Action)
PairAction = tesla_ble_vehicle_ns.class_("PairAction", automation.Action)
RegenerateKeyAction = tesla_ble_vehicle_ns.class_("RegenerateKeyAction", automation.Action)
ForceUpdateAction = tesla_ble_vehicle_ns.class_("ForceUpdateAction", automation.Action)
SetChargingAction = tesla_ble_vehicle_ns.class_("SetChargingAction", automation.Action)
SetChargingAmpsAction = tesla_ble_vehicle_ns.class_("SetChargingAmpsAction", automation.Action)
SetChargingLimitAction = tesla_ble_vehicle_ns.class_("SetChargingLimitAction", automation.Action)

# Configuration constants
CONF_VIN = "vin"
CONF_CHARGING_AMPS_MAX = "charging_amps_max"
CONF_ROLE = "role"

# Configuration constants - no need for custom classes since we use standard ESPHome components
CONF_SLEEP = "sleep"

# Tesla key roles
TESLA_ROLES = {
    "DRIVER": "Keys_Role_ROLE_DRIVER",
    "CHARGING_MANAGER": "Keys_Role_ROLE_CHARGING_MANAGER",
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(TeslaBLEVehicle),
            cv.Required(CONF_VIN): cv.string,
            cv.Optional(CONF_CHARGING_AMPS_MAX, default=32): cv.int_range(min=1, max=48),
            cv.Optional(CONF_ROLE, default="DRIVER"): cv.enum(TESLA_ROLES, upper=True),
        },
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_vin(config[CONF_VIN]))
    
    role = config[CONF_ROLE]
    charging_amps_max = config[CONF_CHARGING_AMPS_MAX]
    
    # Set role and charging configuration
    cg.add(var.set_role(TESLA_ROLES[role]))
    cg.add(var.set_charging_amps_max(charging_amps_max))
    
    # Create built-in entities
    ## Binary sensors
    asleep_sensor = await binary_sensor.new_binary_sensor({
        CONF_ID: cv.declare_id(binary_sensor.BinarySensor)("tesla_asleep_sensor"),
        CONF_NAME: "Asleep",
        CONF_ICON: "mdi:sleep",
        CONF_DISABLED_BY_DEFAULT: False,
    })
    cg.add(var.set_binary_sensor_is_asleep(asleep_sensor))

    doors_sensor = await binary_sensor.new_binary_sensor({
        CONF_ID: cv.declare_id(binary_sensor.BinarySensor)("tesla_doors_sensor"),
        CONF_NAME: "Doors",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_DEVICE_CLASS: binary_sensor.DEVICE_CLASS_LOCK,
    })
    cg.add(var.set_binary_sensor_is_unlocked(doors_sensor))

    user_present_sensor = await binary_sensor.new_binary_sensor({
        CONF_ID: cv.declare_id(binary_sensor.BinarySensor)("tesla_user_present_sensor"),
        CONF_NAME: "User Present",
        CONF_ICON: "mdi:account-check",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_DEVICE_CLASS: binary_sensor.DEVICE_CLASS_OCCUPANCY,
    })
    cg.add(var.set_binary_sensor_is_user_present(user_present_sensor))

    charge_flap_sensor = await binary_sensor.new_binary_sensor({
        CONF_ID: cv.declare_id(binary_sensor.BinarySensor)("tesla_charge_flap_sensor"),
        CONF_NAME: "Charge Flap",
        CONF_ICON: "mdi:ev-plug-tesla",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_DEVICE_CLASS: binary_sensor.DEVICE_CLASS_DOOR,
    })
    cg.add(var.set_binary_sensor_is_charge_flap_open(charge_flap_sensor))

    charger_sensor = await binary_sensor.new_binary_sensor({
        CONF_ID: cv.declare_id(binary_sensor.BinarySensor)("tesla_charger_sensor"),
        CONF_NAME: "Charger",
        CONF_ICON: "mdi:power-plug",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_DEVICE_CLASS: binary_sensor.DEVICE_CLASS_PLUG,
    })
    cg.add(var.set_binary_sensor_is_charger_connected(charger_sensor))

    ## Sensors
    battery_level_sensor = await sensor.new_sensor({
        CONF_ID: cv.declare_id(sensor.Sensor)("tesla_battery_level_sensor"),
        CONF_NAME: "Battery",
        CONF_ICON: "mdi:battery",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_FORCE_UPDATE: False,
        CONF_UNIT_OF_MEASUREMENT: "%",
    })
    cg.add(var.set_battery_level_sensor(battery_level_sensor))

    charger_power_sensor = await sensor.new_sensor({
        CONF_ID: cv.declare_id(sensor.Sensor)("tesla_charger_power_sensor"),
        CONF_NAME: "Charger Power",
        CONF_ICON: "mdi:flash",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_FORCE_UPDATE: False,
        CONF_UNIT_OF_MEASUREMENT: "kW",
    })
    cg.add(var.set_charger_power_sensor(charger_power_sensor))

    charging_rate_sensor = await sensor.new_sensor({
        CONF_ID: cv.declare_id(sensor.Sensor)("tesla_charging_rate_sensor"),
        CONF_NAME: "Charging Rate",
        CONF_ICON: "mdi:speedometer",
        CONF_DEVICE_CLASS: sensor.DEVICE_CLASS_SPEED,
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_FORCE_UPDATE: False,
        CONF_UNIT_OF_MEASUREMENT: "mph",
    })
    cg.add(var.set_charging_rate_sensor(charging_rate_sensor))

    ## Text sensors  
    charging_state_sensor = await text_sensor.new_text_sensor({
        CONF_ID: cv.declare_id(text_sensor.TextSensor)("tesla_charging_state_sensor"),
        CONF_NAME: "Charging",
        CONF_ICON: "mdi:ev-station",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_FORCE_UPDATE: False,
    })
    cg.add(var.set_charging_state_sensor(charging_state_sensor))

    ## Buttons
    wake_button = await button.new_button({
        CONF_ID: cv.declare_id(TeslaWakeButton)("tesla_wake_button"),
        CONF_NAME: "Wake up",
        CONF_ICON: "mdi:sleep-off",
        CONF_DISABLED_BY_DEFAULT: False,
    })
    cg.add(wake_button.set_parent(var))
    cg.add(var.set_wake_button(wake_button))

    pair_button = await button.new_button({
        CONF_ID: cv.declare_id(TeslaPairButton)("tesla_pair_button"),
        CONF_NAME: "Pair BLE Key",
        CONF_ICON: "mdi:key-wireless",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_ENTITY_CATEGORY: cg.EntityCategory.ENTITY_CATEGORY_DIAGNOSTIC,
    })
    cg.add(pair_button.set_parent(var))
    cg.add(var.set_pair_button(pair_button))

    regenerate_key_button = await button.new_button({
        CONF_ID: cv.declare_id(TeslaRegenerateKeyButton)("tesla_regenerate_key_button"),
        CONF_NAME: "Regenerate key",
        CONF_ICON: "mdi:key-change",
        CONF_DISABLED_BY_DEFAULT: True,
        CONF_ENTITY_CATEGORY: cg.EntityCategory.ENTITY_CATEGORY_DIAGNOSTIC,
    })
    cg.add(regenerate_key_button.set_parent(var))
    cg.add(var.set_regenerate_key_button(regenerate_key_button))

    force_update_button = await button.new_button({
        CONF_ID: cv.declare_id(TeslaForceUpdateButton)("tesla_force_update_button"),
        CONF_NAME: "Force data update",
        CONF_ICON: "mdi:database-sync",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_ENTITY_CATEGORY: cg.EntityCategory.ENTITY_CATEGORY_DIAGNOSTIC,
    })
    cg.add(force_update_button.set_parent(var))
    cg.add(var.set_force_update_button(force_update_button))

    ## Switches
    charger_switch = await switch.new_switch({
        CONF_ID: cv.declare_id(TeslaChargingSwitch)("tesla_charger_switch"),
        CONF_NAME: "Charging",
        CONF_ICON: "mdi:ev-station",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_RESTORE_MODE: switch.RESTORE_MODES['RESTORE_DEFAULT_OFF'],
        # CONF_OPTIMISTIC: False,
        # CONF_RESTORE_VALUE: False,
    })
    cg.add(charger_switch.set_parent(var))
    cg.add(var.set_charging_switch(charger_switch))

    ## Numbers
    charging_amps_number = await number.new_number({
        CONF_ID: cv.declare_id(TeslaChargingAmpsNumber)("tesla_charging_amps_number"),
        CONF_NAME: "Charging Amps",
        CONF_ICON: "mdi:current-ac",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_MODE: number.NUMBER_MODES['AUTO'],
        # CONF_OPTIMISTIC: False,
        # CONF_RESTORE_VALUE: False,
        CONF_UNIT_OF_MEASUREMENT: "A",
    }, min_value=0, max_value=charging_amps_max, step=1)
    cg.add(charging_amps_number.set_parent(var))
    cg.add(var.set_charging_amps_number(charging_amps_number))

    charging_limit_number = await number.new_number({
        CONF_ID: cv.declare_id(TeslaChargingLimitNumber)("tesla_charging_limit_number"),
        CONF_NAME: "Charging Limit",
        CONF_ICON: "mdi:battery-charging-100",
        CONF_DISABLED_BY_DEFAULT: False,
        CONF_MODE: number.NUMBER_MODES['AUTO'],
        # CONF_OPTIMISTIC: False,
        # CONF_RESTORE_VALUE: False,
        CONF_UNIT_OF_MEASUREMENT: "%",
    }, min_value=50, max_value=100, step=1)
    cg.add(charging_limit_number.set_parent(var))
    cg.add(var.set_charging_limit_number(charging_limit_number))

# Action schemas
TESLA_WAKE_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
})

TESLA_PAIR_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
})

TESLA_REGENERATE_KEY_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
})

TESLA_FORCE_UPDATE_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
})

TESLA_SET_CHARGING_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
    cv.Required("state"): cv.templatable(cv.boolean),
})

TESLA_SET_CHARGING_AMPS_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
    cv.Required("amps"): cv.templatable(cv.int_range(min=0, max=80)),
})

TESLA_SET_CHARGING_LIMIT_ACTION_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.use_id(TeslaBLEVehicle),
    cv.Required("limit"): cv.templatable(cv.int_range(min=50, max=100)),
})


# Register actions
@automation.register_action(
    "tesla_ble_vehicle.wake", WakeAction, TESLA_WAKE_ACTION_SCHEMA
)
async def tesla_wake_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "tesla_ble_vehicle.pair", PairAction, TESLA_PAIR_ACTION_SCHEMA
)
async def tesla_pair_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "tesla_ble_vehicle.regenerate_key", RegenerateKeyAction, TESLA_REGENERATE_KEY_ACTION_SCHEMA
)
async def tesla_regenerate_key_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "tesla_ble_vehicle.force_update", ForceUpdateAction, TESLA_FORCE_UPDATE_ACTION_SCHEMA
)
async def tesla_force_update_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "tesla_ble_vehicle.set_charging", SetChargingAction, TESLA_SET_CHARGING_ACTION_SCHEMA
)
async def tesla_set_charging_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config["state"], args, bool)
    cg.add(var.set_state(template_))
    return var


@automation.register_action(
    "tesla_ble_vehicle.set_charging_amps", SetChargingAmpsAction, TESLA_SET_CHARGING_AMPS_ACTION_SCHEMA
)
async def tesla_set_charging_amps_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config["amps"], args, int)
    cg.add(var.set_amps(template_))
    return var


@automation.register_action(
    "tesla_ble_vehicle.set_charging_limit", SetChargingLimitAction, TESLA_SET_CHARGING_LIMIT_ACTION_SCHEMA
)
async def tesla_set_charging_limit_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config["limit"], args, int)
    cg.add(var.set_limit(template_))
    return var


