import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT, CONF_SENSOR_ID
from esphome.core import CORE

CODEOWNERS = ["@norton120"]
DEPENDENCIES = ["wifi"]

signalk_ns = cg.esphome_ns.namespace("signalk")
SignalK = signalk_ns.class_("SignalK", cg.Component)
AlarmWhen = signalk_ns.enum("AlarmWhen", is_class=True)

CONF_TARGET = "target"
CONF_SOURCE = "source"
CONF_RESEND_INTERVAL = "resend_interval"
CONF_SENSORS = "sensors"
CONF_ALARMS = "alarms"
CONF_PATH = "path"
CONF_MULTIPLY = "multiply"
CONF_OFFSET = "offset"
CONF_WHEN = "when"
CONF_MESSAGE = "message"
CONF_STATE = "state"
CONF_DELAY = "delay"
CONF_IGNORE_MASK = "ignore_mask"

ALARM_WHEN = {
    "nonzero": AlarmWhen.NONZERO,
    "missing": AlarmWhen.MISSING,
}

# Signal K notification states, least to most severe.
NOTIFICATION_STATES = ["alert", "warn", "alarm", "emergency"]


def _no_json_specials(value):
    # Paths and messages are spliced into the delta JSON verbatim, not escaped.
    value = cv.string_strict(value)
    if any(c in value for c in '"\\') or any(ord(c) < 0x20 for c in value):
        raise cv.Invalid("must not contain quotes, backslashes or control characters")
    return value


def _sk_path(value):
    value = _no_json_specials(value)
    if not value or value.startswith(".") or value.endswith(".") or ".." in value:
        raise cv.Invalid(f"'{value}' is not a valid Signal K path")
    return value


SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR_ID): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PATH): _sk_path,
        cv.Optional(CONF_MULTIPLY, default=1.0): cv.float_,
        cv.Optional(CONF_OFFSET, default=0.0): cv.float_,
    }
)

ALARM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR_ID): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PATH): _sk_path,
        cv.Required(CONF_WHEN): cv.enum(ALARM_WHEN, lower=True),
        cv.Required(CONF_MESSAGE): _no_json_specials,
        cv.Optional(CONF_STATE, default="alarm"): cv.one_of(*NOTIFICATION_STATES, lower=True),
        # How long the condition must hold before the alarm is raised.
        cv.Optional(CONF_DELAY, default="60s"): cv.positive_time_period_milliseconds,
        # `nonzero` only: bits that are status flags rather than faults.
        cv.Optional(CONF_IGNORE_MASK, default=0): cv.hex_uint32_t,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SignalK),
        # Limited broadcast by default, so every Signal K server on the LAN
        # (primary and standby OpenPlotter) gets the same feed with no per-host
        # config on this side.
        cv.Optional(CONF_TARGET, default="255.255.255.255"): cv.ipv4address,
        cv.Optional(CONF_PORT, default=4123): cv.port,
        cv.Optional(CONF_SOURCE): _no_json_specials,
        # Alarm states are re-sent on this interval, not just on change, so a
        # lost UDP packet or a Signal K server that starts later still converges.
        cv.Optional(CONF_RESEND_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SENSORS, default=[]): cv.ensure_list(SENSOR_SCHEMA),
        cv.Optional(CONF_ALARMS, default=[]): cv.ensure_list(ALARM_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_target(str(config[CONF_TARGET])))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_source(config.get(CONF_SOURCE, CORE.name)))
    cg.add(var.set_resend_interval(config[CONF_RESEND_INTERVAL]))

    for conf in config[CONF_SENSORS]:
        sens = await cg.get_variable(conf[CONF_SENSOR_ID])
        cg.add(var.add_sensor(sens, conf[CONF_PATH], conf[CONF_MULTIPLY], conf[CONF_OFFSET]))

    for conf in config[CONF_ALARMS]:
        sens = await cg.get_variable(conf[CONF_SENSOR_ID])
        cg.add(
            var.add_alarm(
                sens, conf[CONF_PATH], conf[CONF_WHEN], conf[CONF_MESSAGE], conf[CONF_STATE], conf[CONF_DELAY],
                conf[CONF_IGNORE_MASK],
            )
        )
