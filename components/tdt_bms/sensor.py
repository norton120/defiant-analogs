import esphome.codegen as cg
from esphome.components import ble_client, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_CURRENT,
    CONF_ID,
    CONF_POWER,
    CONF_VOLTAGE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_EMPTY,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import TdtBms

CONF_CYCLES = "cycles"
CONF_CELL_VOLTAGE_MIN = "cell_voltage_min"
CONF_CELL_VOLTAGE_MAX = "cell_voltage_max"
CONF_CELL_VOLTAGE_DELTA = "cell_voltage_delta"
CONF_TEMPERATURE_AMBIENT = "temperature_ambient"
CONF_TEMPERATURE_MOSFET = "temperature_mosfet"
CONF_TEMPERATURE_CELL_1 = "temperature_cell_1"
CONF_TEMPERATURE_CELL_2 = "temperature_cell_2"
CONF_TEMPERATURE_CELL_3 = "temperature_cell_3"
CONF_TEMPERATURE_CELL_4 = "temperature_cell_4"
CONF_PROBLEM_CODE = "problem_code"

_TEMPERATURE_CELL_KEYS = (
    CONF_TEMPERATURE_CELL_1,
    CONF_TEMPERATURE_CELL_2,
    CONF_TEMPERATURE_CELL_3,
    CONF_TEMPERATURE_CELL_4,
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TdtBms),
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CYCLES): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CELL_VOLTAGE_MIN): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CELL_VOLTAGE_MAX): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CELL_VOLTAGE_DELTA): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMPERATURE_AMBIENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMPERATURE_MOSFET): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_PROBLEM_CODE): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            **{
                cv.Optional(key): sensor.sensor_schema(
                    unit_of_measurement=UNIT_CELSIUS,
                    accuracy_decimals=1,
                    device_class=DEVICE_CLASS_TEMPERATURE,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                    state_class=STATE_CLASS_MEASUREMENT,
                )
                for key in _TEMPERATURE_CELL_KEYS
            },
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.polling_component_schema("30s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    if voltage_config := config.get(CONF_VOLTAGE):
        sens = await sensor.new_sensor(voltage_config)
        cg.add(var.set_voltage_sensor(sens))
    if current_config := config.get(CONF_CURRENT):
        sens = await sensor.new_sensor(current_config)
        cg.add(var.set_current_sensor(sens))
    if power_config := config.get(CONF_POWER):
        sens = await sensor.new_sensor(power_config)
        cg.add(var.set_power_sensor(sens))
    if battery_level_config := config.get(CONF_BATTERY_LEVEL):
        sens = await sensor.new_sensor(battery_level_config)
        cg.add(var.set_battery_level_sensor(sens))
    if cycles_config := config.get(CONF_CYCLES):
        sens = await sensor.new_sensor(cycles_config)
        cg.add(var.set_cycles_sensor(sens))
    if cell_min_config := config.get(CONF_CELL_VOLTAGE_MIN):
        sens = await sensor.new_sensor(cell_min_config)
        cg.add(var.set_cell_voltage_min_sensor(sens))
    if cell_max_config := config.get(CONF_CELL_VOLTAGE_MAX):
        sens = await sensor.new_sensor(cell_max_config)
        cg.add(var.set_cell_voltage_max_sensor(sens))
    if cell_delta_config := config.get(CONF_CELL_VOLTAGE_DELTA):
        sens = await sensor.new_sensor(cell_delta_config)
        cg.add(var.set_cell_voltage_delta_sensor(sens))
    if temp_ambient_config := config.get(CONF_TEMPERATURE_AMBIENT):
        sens = await sensor.new_sensor(temp_ambient_config)
        cg.add(var.set_temperature_ambient_sensor(sens))
    if temp_mosfet_config := config.get(CONF_TEMPERATURE_MOSFET):
        sens = await sensor.new_sensor(temp_mosfet_config)
        cg.add(var.set_temperature_mosfet_sensor(sens))
    for idx, key in enumerate(_TEMPERATURE_CELL_KEYS):
        if temp_cell_config := config.get(key):
            sens = await sensor.new_sensor(temp_cell_config)
            cg.add(var.set_temperature_cell_sensor(idx, sens))
    if problem_code_config := config.get(CONF_PROBLEM_CODE):
        sens = await sensor.new_sensor(problem_code_config)
        cg.add(var.set_problem_code_sensor(sens))
