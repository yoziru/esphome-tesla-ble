import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, binary_sensor, button, switch, number, sensor, text_sensor, lock, cover, climate
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
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
AUTO_LOAD = ["binary_sensor", "button", "switch", "number", "sensor", "text_sensor", "lock", "cover", "climate"]

tesla_ble_vehicle_ns = cg.esphome_ns.namespace("tesla_ble_vehicle")
TeslaBLEVehicle = tesla_ble_vehicle_ns.class_(
    "TeslaBLEVehicle", cg.PollingComponent, ble_client.BLEClientNode
)

# Custom button classes - generated via macro in C++, just reference here
# The class name follows pattern: Tesla{Id}Button where Id is PascalCase of id
TeslaWakeButton = tesla_ble_vehicle_ns.class_("TeslaWakeButton", button.Button)
TeslaPairButton = tesla_ble_vehicle_ns.class_("TeslaPairButton", button.Button)
TeslaRegenerateKeyButton = tesla_ble_vehicle_ns.class_("TeslaRegenerateKeyButton", button.Button)
TeslaForceUpdateButton = tesla_ble_vehicle_ns.class_("TeslaForceUpdateButton", button.Button)
TeslaFlashLightsButton = tesla_ble_vehicle_ns.class_("TeslaFlashLightsButton", button.Button)
TeslaHonkHornButton = tesla_ble_vehicle_ns.class_("TeslaHonkHornButton", button.Button)
TeslaUnlatchDriverDoorButton = tesla_ble_vehicle_ns.class_("TeslaUnlatchDriverDoorButton", button.Button)

# Custom switch classes - generated via macro in C++, just reference here
TeslaChargingSwitch = tesla_ble_vehicle_ns.class_("TeslaChargingSwitch", switch.Switch)
TeslaSteeringWheelHeatSwitch = tesla_ble_vehicle_ns.class_("TeslaSteeringWheelHeatSwitch", switch.Switch)
TeslaSentryModeSwitch = tesla_ble_vehicle_ns.class_("TeslaSentryModeSwitch", switch.Switch)

# Custom lock classes
TeslaDoorsLock = tesla_ble_vehicle_ns.class_("TeslaDoorsLock", lock.Lock)
TeslaChargePortLatchLock = tesla_ble_vehicle_ns.class_("TeslaChargePortLatchLock", lock.Lock)

# Custom cover classes
TeslaTrunkCover = tesla_ble_vehicle_ns.class_("TeslaTrunkCover", cover.Cover)
TeslaFrunkCover = tesla_ble_vehicle_ns.class_("TeslaFrunkCover", cover.Cover)
TeslaWindowsCover = tesla_ble_vehicle_ns.class_("TeslaWindowsCover", cover.Cover)
TeslaChargePortDoorCover = tesla_ble_vehicle_ns.class_("TeslaChargePortDoorCover", cover.Cover)

# Custom climate class
TeslaClimate = tesla_ble_vehicle_ns.class_("TeslaClimate", climate.Climate)

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

# Polling configuration constants
CONF_VCSEC_POLL_INTERVAL = "vcsec_poll_interval"
CONF_INFOTAINMENT_POLL_INTERVAL_AWAKE = "infotainment_poll_interval_awake" 
CONF_INFOTAINMENT_POLL_INTERVAL_ACTIVE = "infotainment_poll_interval_active"
CONF_INFOTAINMENT_SLEEP_TIMEOUT = "infotainment_sleep_timeout"

# Tesla key roles
TESLA_ROLES = {
    "DRIVER": "Keys_Role_ROLE_DRIVER",
    "CHARGING_MANAGER": "Keys_Role_ROLE_CHARGING_MANAGER",
}

# =============================================================================
# ENTITY DEFINITIONS - Add new sensors/controls here!
# =============================================================================
# Just add to these lists - no C++ changes needed unless custom logic is required.
# The sensor ID must match the ID used in vehicle_state_manager.cpp update methods.
#
# Each definition is a dict with:
#   - id: unique identifier (must match C++ usage)
#   - name: display name
#   - icon: MDI icon (optional)
#   - device_class: ESPHome device class (optional)
#   - unit: unit of measurement (optional, for sensors/numbers)
#   - accuracy_decimals: number of decimals for display precision (optional, for sensors only)
#   - disabled_by_default: whether disabled by default (optional, default False)
#   - entity_category: entity category (optional, e.g. "diagnostic")
#
# For buttons/switches, also include:
#   - class: the C++ class reference (e.g. TeslaWakeButton)
#   - setter: setter method name if needed for special handling (optional)
#
# For numbers, also include:
#   - min: minimum value
#   - max: maximum value (or "config" to use config value like charging_amps_max)
#   - step: step size

BINARY_SENSORS = [
    # VCSEC sensors
    {"id": "asleep", "name": "Asleep", "icon": "mdi:sleep"},
    {"id": "user_present", "name": "User Present", "icon": "mdi:account-check", "device_class": "occupancy"},
    {"id": "charger", "name": "Charger", "icon": "mdi:power-plug", "device_class": "plug"},
    
    # Drive sensors
    {"id": "parking_brake", "name": "Parking Brake", "icon": "mdi:car-brake-parking"},
    
    # Individual closure sensors (disabled by default since covers/locks show aggregate state)
    {"id": "door_driver_front", "name": "Door Driver Front", "icon": "mdi:car-door", "device_class": "door", "disabled_by_default": True},
    {"id": "door_driver_rear", "name": "Door Driver Rear", "icon": "mdi:car-door", "device_class": "door", "disabled_by_default": True},
    {"id": "door_passenger_front", "name": "Door Passenger Front", "icon": "mdi:car-door", "device_class": "door", "disabled_by_default": True},
    {"id": "door_passenger_rear", "name": "Door Passenger Rear", "icon": "mdi:car-door", "device_class": "door", "disabled_by_default": True},
    {"id": "window_driver_front", "name": "Window Driver Front", "icon": "mdi:car-door", "device_class": "window", "disabled_by_default": True},
    {"id": "window_driver_rear", "name": "Window Driver Rear", "icon": "mdi:car-door", "device_class": "window", "disabled_by_default": True},
    {"id": "window_passenger_front", "name": "Window Passenger Front", "icon": "mdi:car-door", "device_class": "window", "disabled_by_default": True},
    {"id": "window_passenger_rear", "name": "Window Passenger Rear", "icon": "mdi:car-door", "device_class": "window", "disabled_by_default": True},
    {"id": "sunroof", "name": "Sunroof", "icon": "mdi:car-select", "device_class": "window", "disabled_by_default": True},
]

SENSORS = [
    # Charge state sensors
    {"id": "battery_level", "name": "Battery", "icon": "mdi:battery", "unit": "%"},
    {"id": "range", "name": "Range", "icon": "mdi:map-marker-distance", "device_class": "distance", "unit": "mi"},
    {"id": "charger_power", "name": "Charger Power", "icon": "mdi:flash", "device_class": "power", "unit": "kW"},
    {"id": "charger_voltage", "name": "Charger Voltage", "icon": "mdi:lightning-bolt", "device_class": "voltage", "unit": "V"},
    {"id": "charger_current", "name": "Charger Current", "icon": "mdi:current-ac", "device_class": "current", "unit": "A"},
    {"id": "charging_rate", "name": "Charging Rate", "icon": "mdi:speedometer", "device_class": "speed", "unit": "mph", "accuracy_decimals": 1},
    {"id": "energy_added", "name": "Energy Added", "icon": "mdi:battery-charging", "device_class": "energy", "unit": "kWh", "accuracy_decimals": 1},
    {"id": "time_to_full", "name": "Time to Full", "icon": "mdi:clock-outline", "device_class": "duration", "unit": "min"},
    
    # Climate state sensors
    {"id": "outside_temp", "name": "Outside Temperature", "icon": "mdi:thermometer", "device_class": "temperature", "unit": "°C", "accuracy_decimals": 1},
    
    # Drive state sensors
    {"id": "odometer", "name": "Odometer", "icon": "mdi:counter", "device_class": "distance", "unit": "mi", "disabled_by_default": True},
    
    # Tire pressure sensors
    {"id": "tpms_front_left", "name": "TPMS Front Left", "icon": "mdi:car-tire-alert", "device_class": "pressure", "unit": "bar", "accuracy_decimals": 1},
    {"id": "tpms_front_right", "name": "TPMS Front Right", "icon": "mdi:car-tire-alert", "device_class": "pressure", "unit": "bar", "accuracy_decimals": 1},
    {"id": "tpms_rear_left", "name": "TPMS Rear Left", "icon": "mdi:car-tire-alert", "device_class": "pressure", "unit": "bar", "accuracy_decimals": 1},
    {"id": "tpms_rear_right", "name": "TPMS Rear Right", "icon": "mdi:car-tire-alert", "device_class": "pressure", "unit": "bar", "accuracy_decimals": 1},
]

TEXT_SENSORS = [
    {"id": "charging_state", "name": "Charging", "icon": "mdi:ev-station"},
    {"id": "iec61851_state", "name": "IEC 61851", "icon": "mdi:ev-plug-type2", "disabled_by_default": True},
    {"id": "shift_state", "name": "Shift State", "icon": "mdi:car-shift-pattern", "disabled_by_default": True},
]

BUTTONS = [
    {"id": "wake", "name": "Wake up", "class": TeslaWakeButton, "setter": "set_wake_button", "icon": "mdi:sleep-off"},
    {"id": "pair", "name": "Pair BLE Key", "class": TeslaPairButton, "setter": "set_pair_button", "icon": "mdi:key-wireless", "entity_category": "diagnostic"},
    {"id": "regenerate_key", "name": "Regenerate key", "class": TeslaRegenerateKeyButton, "setter": "set_regenerate_key_button", "icon": "mdi:key-change", "entity_category": "diagnostic", "disabled_by_default": True},
    {"id": "force_update", "name": "Force data update", "class": TeslaForceUpdateButton, "setter": "set_force_update_button", "icon": "mdi:database-sync", "entity_category": "diagnostic"},
    # Unique actions (not part of combined entities)
    {"id": "unlatch_driver_door", "name": "Unlatch Driver Door", "class": TeslaUnlatchDriverDoorButton, "setter": None, "icon": "mdi:car-door", "disabled_by_default": True},
    # Vehicle controls
    {"id": "flash_lights", "name": "Flash Lights", "class": TeslaFlashLightsButton, "setter": None, "icon": "mdi:car-light-high"},
    {"id": "honk_horn", "name": "Sound Horn", "class": TeslaHonkHornButton, "setter": None, "icon": "mdi:bullhorn"},
]

SWITCHES = [
    {"id": "charging", "name": "Charger", "class": TeslaChargingSwitch, "setter": "set_charging_switch", "icon": "mdi:ev-station"},
    {"id": "steering_wheel_heat", "name": "Heated Steering", "class": TeslaSteeringWheelHeatSwitch, "setter": "set_steering_wheel_heat_switch", "icon": "mdi:steering"},
    {"id": "sentry_mode", "name": "Sentry Mode", "class": TeslaSentryModeSwitch, "setter": "set_sentry_mode_switch", "icon": "mdi:shield-car"},
]

# Lock entities (combined sensor + control)
LOCKS = [
    {"id": "doors", "name": "Doors", "class": TeslaDoorsLock, "setter": "set_doors_lock", "icon": "mdi:car-door-lock"},
    {"id": "charge_port_latch", "name": "Charge Port Latch", "class": TeslaChargePortLatchLock, "setter": "set_charge_port_latch_lock", "icon": "mdi:ev-plug-tesla"},
]

# Cover entities (combined sensor + control)
COVERS = [
    {"id": "trunk", "name": "Trunk", "class": TeslaTrunkCover, "setter": "set_trunk_cover", "icon": "mdi:car-back", "device_class": "door"},
    {"id": "frunk", "name": "Frunk", "class": TeslaFrunkCover, "setter": "set_frunk_cover", "icon": "mdi:car", "device_class": "door"},
    {"id": "windows", "name": "Windows", "class": TeslaWindowsCover, "setter": "set_windows_cover", "icon": "mdi:car-door", "device_class": "awning"},
    {"id": "charge_port_door", "name": "Charge Port Door", "class": TeslaChargePortDoorCover, "setter": "set_charge_port_door_cover", "icon": "mdi:ev-plug-tesla", "device_class": "door"},
]

# Climate entity
CLIMATE = {
    "id": "climate",
    "name": "Climate",
    "class": TeslaClimate,
    "setter": "set_climate",
}

NUMBERS = [
    {
        "id": "charging_amps",
        "name": "Charging Amps",
        "class": TeslaChargingAmpsNumber,
        "setter": "set_charging_amps_number",
        "icon": "mdi:current-ac",
        "unit": "A",
        "min": 0,
        "max": "config",  # Will use charging_amps_max from config
        "step": 1,
    },
    {
        "id": "charging_limit",
        "name": "Charging Limit",
        "class": TeslaChargingLimitNumber,
        "setter": "set_charging_limit_number",
        "icon": "mdi:battery-charging-100",
        "unit": "%",
        "min": 50,
        "max": 100,
        "step": 1,
    },
]

# =============================================================================
# CONFIG SCHEMA
# =============================================================================

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(TeslaBLEVehicle),
            cv.Required(CONF_VIN): cv.string,
            cv.Optional(CONF_CHARGING_AMPS_MAX, default=32): cv.int_range(min=1, max=48),
            cv.Optional(CONF_ROLE, default="DRIVER"): cv.enum(TESLA_ROLES, upper=True),
            # Polling intervals (in seconds)
            cv.Optional(CONF_VCSEC_POLL_INTERVAL, default=10): cv.int_range(min=5, max=300),
            cv.Optional(CONF_INFOTAINMENT_POLL_INTERVAL_AWAKE, default=30): cv.int_range(min=10, max=600), 
            cv.Optional(CONF_INFOTAINMENT_POLL_INTERVAL_ACTIVE, default=10): cv.int_range(min=5, max=120),
            cv.Optional(CONF_INFOTAINMENT_SLEEP_TIMEOUT, default=660): cv.int_range(min=60, max=3600),
        },
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

def get_device_class_const(component_module, device_class_str):
    """Convert device class string to the actual constant."""
    if device_class_str is None:
        return None
    return getattr(component_module, f"DEVICE_CLASS_{device_class_str.upper()}", None)


async def create_binary_sensor(var, definition):
    """Create a binary sensor and register with TeslaBLEVehicle using generic setter."""
    config = {
        CONF_ID: cv.declare_id(binary_sensor.BinarySensor)(f"tesla_{definition['id']}_sensor"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    if "device_class" in definition:
        dc = get_device_class_const(binary_sensor, definition["device_class"])
        if dc:
            config[CONF_DEVICE_CLASS] = dc
    
    sens = await binary_sensor.new_binary_sensor(config)
    # Use generic setter with sensor ID
    cg.add(var.set_binary_sensor(definition["id"], sens))
    return sens


async def create_sensor(var, definition):
    """Create a sensor and register with TeslaBLEVehicle using generic setter."""
    config = {
        CONF_ID: cv.declare_id(sensor.Sensor)(f"tesla_{definition['id']}_sensor"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
        CONF_FORCE_UPDATE: False,
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    if "unit" in definition:
        config[CONF_UNIT_OF_MEASUREMENT] = definition["unit"]
    if "accuracy_decimals" in definition:
        config[CONF_ACCURACY_DECIMALS] = definition["accuracy_decimals"]
    if "device_class" in definition:
        dc = get_device_class_const(sensor, definition["device_class"])
        if dc:
            config[CONF_DEVICE_CLASS] = dc
    
    sens = await sensor.new_sensor(config)
    # Use generic setter with sensor ID
    cg.add(var.set_sensor(definition["id"], sens))
    return sens


async def create_text_sensor(var, definition):
    """Create a text sensor and register with TeslaBLEVehicle using generic setter."""
    config = {
        CONF_ID: cv.declare_id(text_sensor.TextSensor)(f"tesla_{definition['id']}_sensor"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
        CONF_FORCE_UPDATE: False,
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    
    sens = await text_sensor.new_text_sensor(config)
    # Use generic setter with sensor ID
    cg.add(var.set_text_sensor(definition["id"], sens))
    return sens


async def create_button(var, definition):
    """Create a button and register with TeslaBLEVehicle."""
    config = {
        CONF_ID: cv.declare_id(definition["class"])(f"tesla_{definition['id']}_button"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    if "entity_category" in definition:
        if definition["entity_category"] == "diagnostic":
            config[CONF_ENTITY_CATEGORY] = cg.EntityCategory.ENTITY_CATEGORY_DIAGNOSTIC
    
    btn = await button.new_button(config)
    cg.add(btn.set_parent(var))
    if definition.get("setter"):
        cg.add(getattr(var, definition["setter"])(btn))
    return btn


async def create_switch(var, definition):
    """Create a switch and register with TeslaBLEVehicle."""
    config = {
        CONF_ID: cv.declare_id(definition["class"])(f"tesla_{definition['id']}_switch"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
        CONF_RESTORE_MODE: switch.RESTORE_MODES['RESTORE_DEFAULT_OFF'],
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    
    sw = await switch.new_switch(config)
    cg.add(sw.set_parent(var))
    if definition.get("setter"):
        cg.add(getattr(var, definition["setter"])(sw))
    return sw


async def create_number(var, definition, config):
    """Create a number and register with TeslaBLEVehicle."""
    # Handle dynamic max value from config
    max_val = definition["max"]
    if max_val == "config":
        max_val = config.get(CONF_CHARGING_AMPS_MAX, 32)
    
    num_config = {
        CONF_ID: cv.declare_id(definition["class"])(f"tesla_{definition['id']}_number"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
        CONF_MODE: number.NUMBER_MODES['AUTO'],
    }
    if "icon" in definition:
        num_config[CONF_ICON] = definition["icon"]
    if "unit" in definition:
        num_config[CONF_UNIT_OF_MEASUREMENT] = definition["unit"]
    
    num = await number.new_number(
        num_config,
        min_value=definition["min"],
        max_value=max_val,
        step=definition["step"]
    )
    cg.add(num.set_parent(var))
    if definition.get("setter"):
        cg.add(getattr(var, definition["setter"])(num))
    return num


async def create_lock(var, definition):
    """Create a lock and register with TeslaBLEVehicle."""
    config = {
        CONF_ID: cv.declare_id(definition["class"])(f"tesla_{definition['id']}_lock"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    
    lck = cg.new_Pvariable(config[CONF_ID])
    await lock.register_lock(lck, config)
    cg.add(lck.set_parent(var))
    if definition.get("setter"):
        cg.add(getattr(var, definition["setter"])(lck))
    return lck


async def create_cover(var, definition):
    """Create a cover and register with TeslaBLEVehicle."""
    config = {
        CONF_ID: cv.declare_id(definition["class"])(f"tesla_{definition['id']}_cover"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: definition.get("disabled_by_default", False),
    }
    if "icon" in definition:
        config[CONF_ICON] = definition["icon"]
    if "device_class" in definition:
        config[CONF_DEVICE_CLASS] = definition["device_class"]
    
    cvr = cg.new_Pvariable(config[CONF_ID])
    await cover.register_cover(cvr, config)
    cg.add(cvr.set_parent(var))
    if definition.get("setter"):
        cg.add(getattr(var, definition["setter"])(cvr))
    return cvr


async def create_climate_entity(var, definition):
    """Create a climate entity and register with TeslaBLEVehicle."""
    from esphome.components.climate import CONF_VISUAL
    
    config = {
        CONF_ID: cv.declare_id(definition["class"])(f"tesla_{definition['id']}_climate"),
        CONF_NAME: definition["name"],
        CONF_DISABLED_BY_DEFAULT: False,
        # Visual settings for the climate entity UI
        CONF_VISUAL: {},
        CONF_ACCURACY_DECIMALS: 1,
    }
    
    clm = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(clm, config)
    cg.add(clm.set_parent(var))
    if definition.get("setter"):
        cg.add(getattr(var, definition["setter"])(clm))
    return clm


# =============================================================================
# CODE GENERATION
# =============================================================================

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_vin(config[CONF_VIN]))
    
    role = config[CONF_ROLE]
    charging_amps_max = config[CONF_CHARGING_AMPS_MAX]
    vcsec_interval_seconds = config[CONF_VCSEC_POLL_INTERVAL]
    
    cg.add(var.set_update_interval(vcsec_interval_seconds * 1000))
    cg.add(var.set_role(TESLA_ROLES[role]))
    cg.add(var.set_charging_amps_max(charging_amps_max))
    
    # Set polling intervals (convert from seconds to milliseconds)
    cg.add(var.set_vcsec_poll_interval(vcsec_interval_seconds * 1000))
    cg.add(var.set_infotainment_poll_interval_awake(config[CONF_INFOTAINMENT_POLL_INTERVAL_AWAKE] * 1000))
    cg.add(var.set_infotainment_poll_interval_active(config[CONF_INFOTAINMENT_POLL_INTERVAL_ACTIVE] * 1000))
    cg.add(var.set_infotainment_sleep_timeout(config[CONF_INFOTAINMENT_SLEEP_TIMEOUT] * 1000))
    
    # Create all sensors using data-driven approach with generic setters
    for definition in BINARY_SENSORS:
        await create_binary_sensor(var, definition)
    
    for definition in SENSORS:
        await create_sensor(var, definition)
    
    for definition in TEXT_SENSORS:
        await create_text_sensor(var, definition)
    
    for definition in BUTTONS:
        await create_button(var, definition)
    
    # Switches - data-driven approach
    for definition in SWITCHES:
        await create_switch(var, definition)

    # Numbers - data-driven approach
    for definition in NUMBERS:
        await create_number(var, definition, config)

    # Locks - combined entities for doors and charge port
    for definition in LOCKS:
        await create_lock(var, definition)

    # Covers - combined entities for trunk, frunk, windows
    for definition in COVERS:
        await create_cover(var, definition)

    # Climate - HVAC control
    await create_climate_entity(var, CLIMATE)


# =============================================================================
# ACTION SCHEMAS
# =============================================================================

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


# =============================================================================
# ACTION REGISTRATION
# =============================================================================

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
