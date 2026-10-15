import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_BATTERY_CHARGING, DEVICE_CLASS_PROBLEM, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_TDT_BMS_ID, TdtBms

CONF_CHARGING = "charging"
CONF_DISCHARGING = "discharging"
CONF_PROBLEM = "problem"

# Attaches to the TdtBms instance declared under the `sensor:` platform (same
# ble_client, same device) -- this doesn't open a second BLE connection.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TDT_BMS_ID): cv.use_id(TdtBms),
        cv.Optional(CONF_CHARGING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_DISCHARGING): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PROBLEM): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_TDT_BMS_ID])

    if charging_config := config.get(CONF_CHARGING):
        sens = await binary_sensor.new_binary_sensor(charging_config)
        cg.add(hub.set_charging_binary_sensor(sens))
    if discharging_config := config.get(CONF_DISCHARGING):
        sens = await binary_sensor.new_binary_sensor(discharging_config)
        cg.add(hub.set_discharging_binary_sensor(sens))
    if problem_config := config.get(CONF_PROBLEM):
        sens = await binary_sensor.new_binary_sensor(problem_config)
        cg.add(hub.set_problem_binary_sensor(sens))
