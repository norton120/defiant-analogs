# defiant-analogs

ESPHome firmware for S/V Defiant's ESP32 sensor nodes, plus the OpenPlotter-side
config that displays their data. Everything reports to **Signal K on
OpenPlotter**. Home Assistant was retired on 2026-10-14 and nothing here talks to
it any more.

## Nodes

| Node | Config | What it does |
|---|---|---|
| `defiant-analogs` | `defiant-analogs.yaml` | Fuel sender (KUS 240–33 Ω) via ADC |
| `defiant-bms-bank1` | `bms-bank-1.yaml` | BLE reader for Bank I packs A/B/C |
| `defiant-bms-bank2` | `bms-bank-2.yaml` | BLE reader for Bank II packs A/B/C |
| `defiant-bms-bank3` | `bms-bank-3.yaml` | BLE reader for Bank III packs 1/2/3 (physical A/B/C mapping unknown) |

The BMS packs are WattCycle LiFePO4 with TDT smart-BMS hardware. Each node holds
exactly 3 GATT connections, which is the reliable ceiling for one ESP32 radio.

The `api:` block stays enabled for `esphome logs`. It has `reboot_timeout: 0s`
because no HA client connects any more, and without that setting the nodes reboot
every 15 minutes.

## Components (`components/`)

- **`tdt_bms`**: a BLE protocol port of patman15/aiobmsble's `tdt_bms.py`. It
  includes a raw `problem_code` sensor holding the 16-bit 0x8D fault code.
  Upstream doesn't decode the individual bits, so neither does this.
- **`signalk`**: pushes sensor readings to Signal K as delta JSON over UDP.
  - It broadcasts to `255.255.255.255:4123` by default, so the primary
    OpenPlotter and the warm standby both receive the same feed with no
    per-host config.
  - `$source` comes out as `esphome.<node name>`.
  - `sensors:` entries take `sensor_id`, `path`, `multiply` and `offset`. A NaN
    reading is sent as `null`, so stale values don't linger.
  - `alarms:` entries take `sensor_id`, `path`, `when` (`nonzero` or `missing`),
    `message`, `state` (default `alarm`) and `delay` (default 60s). Each one
    becomes `notifications.<path>`.
  - An alarm is raised only after its condition has held for the whole `delay`.
    While raised it is re-sent every 30s. When it clears, `null` is sent 3 times.
    `nonzero` alarms append the code to the message, e.g. `(code 0x0004)`.

## Signal K paths

Battery keys: `bank1a` `bank1b` `bank1c` `bank2a` `bank2b` `bank2c` `bank3-1`
`bank3-2` `bank3-3`.

| Path | Units | Notes |
|---|---|---|
| `electrical.batteries.<key>.capacity.stateOfCharge` | ratio 0–1 | null after 120s with no BMS reading |
| `electrical.batteries.<key>.current` | A | + = charging |
| `electrical.batteries.<key>.power` | W | + = charging |
| `electrical.batteries.<key>.voltage` | V | |
| `tanks.fuel.0.currentLevel` | ratio 0–1 | null when the sender reads out of range |
| `notifications.electrical.batteries.<key>.bmsFault` | alarm | BMS problem code ≠ 0 for 60s |
| `notifications.electrical.batteries.<key>.notReporting` | alarm | no SoC for 60s |
| `notifications.tanks.fuel.0.senderFault` | warn | sender open, shorted or chafed for 60s |

The **battery wall** total is the mean SoC and summed power of the 9 BMS packs.
The Cerbo's "House Bank" SmartShunt drifts badly over time, so it is
deliberately not used. Right now the wall total is computed only inside the
AvNav widget (see below). It is not a Signal K path.

## Flashing

ESPHome is run locally. The keys and passwords go in `secrets.yaml`; see
`secrets.yaml.example`.

```bash
# Run under bash: zsh doesn't word-split $pair.
bash -c 'for pair in "defiant-analogs.yaml defiant-analogs" "bms-bank-1.yaml defiant-bms-bank1" \
  "bms-bank-2.yaml defiant-bms-bank2" "bms-bank-3.yaml defiant-bms-bank3"; do
  set -- $pair; esphome run $1 --device $2.local --no-logs; done'
```

## OpenPlotter side (openplotter.local)

Signal K runs as user `pi` from `/home/pi/.signalk`, on port 3000.

- **ESP32 feed**: a piped provider with id `esphome` (type SignalK, UDP port
  4123) in `settings.json`.
- **Victron**: the `signalk-venus-plugin` connects over MQTTS to the Pi5 Venus
  (`venus.local`, port 8883). Its config is in
  `plugin-config-data/venus.json`. That gives:
  - Solar from 4 MPPTs at `electrical.solar.<288|289|290|291>.*`, plus the
    total in `electrical.venus.totalPanelPower`.
  - The wind generator (SmartShunt "Windy") at `electrical.dcsource.292.*`.
  - The DC/DC charger, whenever it's plugged in.

  The plugin supports only one host, so the Cerbo (MultiPlus, House Bank shunt)
  isn't connected.
- **`signalk-mqtt-home-assistant`** plugin: disabled.

### MPPT IDs on the Pi5 Venus

The port/starboard mapping is still unknown, so the IDs aren't labelled by
location yet.

| ID | Model |
|---|---|
| 288 | MPPT 100/30 |
| 289 | MPPT 100/30 |
| 290 | MPPT 100/50 |
| 291 | MPPT 75/15 |

### AvNav "Power" page

The source is in `openplotter/avnav/`. On the Pi it lives under
`/home/pi/avnav/data/`, owned by `pi`.

- `defiant-user.js` and `defiant-user.css` are appended to `user/viewer/user.js`
  and `user/viewer/user.css`. They define the widgets `DefiantBatteryPack`,
  `DefiantBatteryWall` and `DefiantSolar`, plus the formatter
  `defiantRatioPercent`. The pre-Defiant originals are kept as
  `*.bak-pre-defiant`.
- `mklayout.py <src layout> <dst>` builds `layout/defiant.json`, which AvNav
  shows as `user.defiant`. It is AvNav's `system.default` layout with dashboard
  page 4 replaced by the Power page: the wall, 9 packs, solar and fuel.

To view the page on a screen, go to AvNav **Settings → Layout → `user.defiant`**
and open dashboard page **4**. The active layout is saved per browser.
