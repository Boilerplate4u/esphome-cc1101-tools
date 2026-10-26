"""RF sniffer for ESPHome remote_receiver.

Decodes every received frame with the Oregon Scientific v2.1 decoder and the
built-in ESPHome RF protocols, and logs one line per transmission with its
repeat count, signal strength (CC1101) and reception time.
"""

import esphome.codegen as cg
from esphome.components import remote_base, sensor, text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_TIME_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_DECIBEL_MILLIWATT,
)

CODEOWNERS = ["@Boilerplate4U"]
DEPENDENCIES = ["remote_receiver"]
AUTO_LOAD = ["sensor", "text_sensor"]

CONF_RECEIVER_ID = "receiver_id"
CONF_CC1101_ID = "cc1101_id"
CONF_RAW_MODE = "raw_mode"
CONF_MIN_PULSES = "min_pulses"
CONF_MAX_REPEAT_WAIT = "max_repeat_wait"
CONF_OREGON = "oregon"
CONF_MIN_PULSE = "min_pulse"
CONF_LONG_PULSE = "long_pulse"
CONF_MAX_PULSE = "max_pulse"
CONF_END_GAP = "end_gap"
CONF_MIN_PREAMBLE = "min_preamble"
CONF_VERIFY_BIT_PAIRS = "verify_bit_pairs"
CONF_STATISTICS = "statistics"
CONF_FRAMES = "frames"
CONF_DECODED = "decoded"
CONF_UNKNOWN = "unknown"
CONF_NOISE_PER_MINUTE = "noise_per_minute"
CONF_NOISE_FLOOR = "noise_floor"
CONF_PER_PROTOCOL = "per_protocol"

rf_sniffer_ns = cg.esphome_ns.namespace("rf_sniffer")
RfSniffer = rf_sniffer_ns.class_(
    "RfSniffer", cg.PollingComponent, remote_base.RemoteReceiverListener
)
RawMode = rf_sniffer_ns.enum("RawMode", True)
RAW_MODES = {
    "off": RawMode.OFF,
    "unknown_only": RawMode.UNKNOWN_ONLY,
    "all": RawMode.ALL,
}
ManchesterTiming = rf_sniffer_ns.namespace("ManchesterDecoder").struct("Timing")
CC1101Component = cg.esphome_ns.namespace("cc1101").class_("CC1101Component")
RealTimeClock = cg.esphome_ns.namespace("time").class_("RealTimeClock")

# Protocols decoded from C++. remote_base only compiles the protocol sources a
# configuration requests (ESPHome 2026.10 and later).
BUILTIN_PROTOCOLS = [
    "nexa",
    "keeloq",
    "dooya",
    "drayton",
    "byronsx",
    "brennenstuhl",
    "rc_switch",
]
# Decoders that only exist in newer ESPHome versions (dev at the time of writing):
# define name -> (protocol name, data class remote_base declares for it).
OPTIONAL_PROTOCOLS = {
    "NEXUS": ("nexus", "NexusData"),
    "AOK": ("aok", "AOKData"),
    "CARDIN_S438": ("cardin_s438", "CardinS438Data"),
}


def _request_protocol(name):
    request = getattr(remote_base, "request_protocol", None)
    if request is None:
        return True  # older ESPHome compiles every protocol source
    try:
        request(name)
    except ValueError:
        return False
    return True


def _validate_timing(config):
    if not (
        config[CONF_MIN_PULSE]
        < config[CONF_LONG_PULSE]
        < config[CONF_MAX_PULSE]
        < config[CONF_END_GAP]
    ):
        raise cv.Invalid(
            "Timing must satisfy min_pulse < long_pulse < max_pulse < end_gap"
        )
    return config


OREGON_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(
                CONF_MIN_PULSE, default="200us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_LONG_PULSE, default="700us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_MAX_PULSE, default="1200us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_END_GAP, default="2500us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(CONF_MIN_PREAMBLE, default=20): cv.int_range(min=8, max=64),
            cv.Optional(CONF_VERIFY_BIT_PAIRS, default=True): cv.boolean,
        }
    ),
    _validate_timing,
)

_COUNTER = sensor.sensor_schema(
    accuracy_decimals=0,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)

STATISTICS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_FRAMES): _COUNTER,
        cv.Optional(CONF_DECODED): _COUNTER,
        cv.Optional(CONF_UNKNOWN): _COUNTER,
        cv.Optional(CONF_NOISE_PER_MINUTE): sensor.sensor_schema(
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_NOISE_FLOOR): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PER_PROTOCOL): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RfSniffer),
        cv.GenerateID(CONF_RECEIVER_ID): cv.use_id(remote_base.RemoteReceiverBase),
        cv.Optional(CONF_CC1101_ID): cv.use_id(CC1101Component),
        cv.Optional(CONF_TIME_ID): cv.use_id(RealTimeClock),
        cv.Optional(CONF_RAW_MODE, default="off"): cv.enum(RAW_MODES, lower=True),
        cv.Optional(CONF_MIN_PULSES, default=60): cv.int_range(min=0, max=1024),
        cv.Optional(CONF_MAX_REPEAT_WAIT, default="500ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(milliseconds=100), max=cv.TimePeriod(seconds=2)),
        ),
        cv.Optional(CONF_OREGON, default={}): OREGON_SCHEMA,
        cv.Optional(CONF_STATISTICS, default={}): STATISTICS_SCHEMA,
    }
).extend(cv.polling_component_schema("60s"))


def _us(value):
    return int(value.total_microseconds)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    receiver = await cg.get_variable(config[CONF_RECEIVER_ID])
    if hasattr(remote_base, "add_listener"):
        # ESPHome 2026.10 and later count listener slots at code generation.
        remote_base.add_listener(receiver, var)
    else:
        cg.add(receiver.register_listener(var))

    for name in BUILTIN_PROTOCOLS:
        _request_protocol(name)
    for define, (name, data_class) in OPTIONAL_PROTOCOLS.items():
        if hasattr(remote_base, data_class) and _request_protocol(name):
            cg.add_define(f"RF_SNIFFER_HAS_{define}")

    if CONF_CC1101_ID in config:
        cg.add_define("USE_RF_SNIFFER_RSSI")
        radio = await cg.get_variable(config[CONF_CC1101_ID])
        cg.add(var.set_radio(radio))
    if CONF_TIME_ID in config:
        cg.add_define("USE_RF_SNIFFER_TIME")
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(clock))

    cg.add(var.set_raw_mode(config[CONF_RAW_MODE]))
    cg.add(var.set_min_pulses(config[CONF_MIN_PULSES]))
    cg.add(
        var.set_max_repeat_wait(int(config[CONF_MAX_REPEAT_WAIT].total_milliseconds))
    )

    oregon = config[CONF_OREGON]
    cg.add(
        var.set_oregon_timing(
            cg.StructInitializer(
                ManchesterTiming,
                ("min_pulse_us", _us(oregon[CONF_MIN_PULSE])),
                ("long_pulse_us", _us(oregon[CONF_LONG_PULSE])),
                ("max_pulse_us", _us(oregon[CONF_MAX_PULSE])),
                ("end_gap_us", _us(oregon[CONF_END_GAP])),
                ("min_preamble", oregon[CONF_MIN_PREAMBLE]),
                ("min_bytes", 8),
            )
        )
    )
    cg.add(var.set_oregon_verify_bit_pairs(oregon[CONF_VERIFY_BIT_PAIRS]))

    stats = config[CONF_STATISTICS]
    for key in (
        CONF_FRAMES,
        CONF_DECODED,
        CONF_UNKNOWN,
        CONF_NOISE_PER_MINUTE,
        CONF_NOISE_FLOOR,
    ):
        if key in stats:
            sens = await sensor.new_sensor(stats[key])
            cg.add(getattr(var, f"set_{key}_sensor")(sens))
    if CONF_PER_PROTOCOL in stats:
        tsens = await text_sensor.new_text_sensor(stats[CONF_PER_PROTOCOL])
        cg.add(var.set_per_protocol_text_sensor(tsens))
