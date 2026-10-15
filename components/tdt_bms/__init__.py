import esphome.codegen as cg
from esphome.components import ble_client

CODEOWNERS = ["@norton120"]
DEPENDENCIES = ["ble_client"]

tdt_bms_ns = cg.esphome_ns.namespace("tdt_bms")
TdtBms = tdt_bms_ns.class_(
    "TdtBms", ble_client.BLEClientNode, cg.PollingComponent
)

CONF_TDT_BMS_ID = "tdt_bms_id"
