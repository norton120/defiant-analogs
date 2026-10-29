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
    `message`, `state` (default `alarm`), `delay` (default 60s) and, for
    `nonzero` only, `ignore_mask` (bits that are status flags rather than
    faults). Each one becomes `notifications.<path>`.
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
| `electrical.batteries.<key>.bmsProblemCode` | raw 16-bit code | 0 = nothing set |
| `tanks.fuel.0.currentLevel` | ratio 0–1 | null when the sender reads out of range |
| `notifications.tanks.fuel.0.senderFault` | warn | sender open, shorted or chafed for 60s |

**Battery alarms.** The BMS nodes deliberately raise no Signal K
notifications. Battery state is shown on the AvNav power page instead; BMS
alarms were noisy and drowned out the alarms that matter (e.g. bilge).

**Problem-code bits.** TDT doesn't document these. On 2026-10-15, every pack
sitting at 100% reported `0x0400`, or `0x0100` in one case, while the packs
below 100% reported 0, so both bits look like "full" status flags. The raw
code is published as `bmsProblemCode` for reference only.

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
  - Solar from 4 MPPTs at `electrical.solar.<name>.*`, plus the total in
    `electrical.venus.totalPanelPower`. With `useDeviceNames` on, the plugin
    keys each charger by its camelCased Venus CustomName (see below).
  - The wind generator (SmartShunt "Windy") at `electrical.dcsource.292.*`.
  - The DC/DC charger (Orion XS 12/12-50A, "Engine to House Bank") at
    `electrical.alternator.291.*`.

  The plugin supports only one host, so the Cerbo (MultiPlus, House Bank shunt)
  isn't connected.
- **`signalk-mqtt-home-assistant`** plugin: disabled.

### MPPTs on the Pi5 Venus

| Venus ID | CustomName → SK key | Model |
|---|---|---|
| 288 | Starboard Bimini → `starboardBimini` | MPPT 100/30 |
| 289 | Port Bimini → `portBimini` | MPPT 100/30 |
| 290 | Port Davits → `portDavits` | MPPT 100/50 |
| 291 | Starboard Davits → `starboardDavits` | MPPT 75/15 |

The davits mapping came from the owner. The two 100/30 bimini chargers were
told apart from two weeks of daily yield. The south-facing starboard side
outproduced port lately, and 288 beat 289 on every sunny day (about 1.6×), so
288 is starboard. If that ever looks wrong, shade one bimini panel and watch
which reading drops.

CustomNames were set over Venus MQTT
(`W/<portal id>/solarcharger/<id>/CustomName`). If you rename a charger, the
venus plugin only picks up the new key after a Signal K restart, and the
`MPPTS` list in `defiant-user.js` has to be updated to match.

### AvNav "Power" page

The source is in `openplotter/avnav/`. On the Pi it lives under
`/home/pi/avnav/data/`, owned by `pi`.

- `defiant-user.js` and `defiant-user.css` are appended to `user/viewer/user.js`
  and `user/viewer/user.css`. They define the widgets `DefiantBatteryPack`,
  `DefiantBatteryWall`, `DefiantSolar` (total, port vs. starboard, per
  charger) and `DefiantDcDc`, plus the formatter
  `defiantRatioPercent`. The pre-Defiant originals are kept as
  `*.bak-pre-defiant`.
- `mklayout.py <src layout> <dst>` builds `layout/defiant.json`, which AvNav
  shows as `user.defiant`. It is AvNav's `system.default` layout with dashboard
  page 4 replaced by the Power page: the wall, 9 packs, DC/DC, solar and fuel.

Avoid `mode` as a widget store-key name. It clashes with AvNav's own `mode`
prop, and the widget then renders as "Undefined Widget".

To view the page on a screen, go to AvNav **Settings → Layout → `user.defiant`**
and open dashboard page **4**. The active layout is saved per browser.
