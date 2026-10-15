"""Build user.defiant.json: AvNav's default layout with dashboard page 4 replaced by a Power page."""
import json
import sys

SK = "nav.gps.signalk."
src, dst = sys.argv[1], sys.argv[2]
layout = json.load(open(src))


def pack(key, caption):
    base = SK + "electrical.batteries." + key
    return {
        "name": "DefiantBatteryPack",
        "caption": caption,
        "storeKeys": {"soc": base + ".capacity.stateOfCharge", "power": base + ".power"},
    }


fuel = {
    "name": "Default",
    "caption": "Fuel",
    "unit": "",
    "formatter": "defiantRatioPercent",
    "storeKeys": {"value": SK + "tanks.fuel.0.currentLevel"},
}

layout["description"] = "Defiant: default layout, dashboard page 4 = Power"
layout["widgets"]["gpspage4"] = {
    "left": [
        {"name": "DefiantBatteryWall", "caption": "Battery Wall", "weight": 1.5},
        pack("bank1a", "Bank I A"),
        pack("bank1b", "Bank I B"),
        pack("bank1c", "Bank I C"),
        pack("bank2a", "Bank II A"),
        pack("bank2b", "Bank II B"),
        {"name": "DefiantDcDc", "caption": "DC/DC (engine)"},
    ],
    "right": [
        {"name": "DefiantSolar", "caption": "Solar", "weight": 2.3},
        pack("bank2c", "Bank II C"),
        pack("bank3-1", "Bank III 1"),
        pack("bank3-2", "Bank III 2"),
        pack("bank3-3", "Bank III 3"),
        fuel,
    ],
}
json.dump(layout, open(dst, "w"), indent=1)
print("wrote", dst)
