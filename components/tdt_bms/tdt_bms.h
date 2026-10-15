#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_ESP32

#include <esp_gattc_api.h>
#include <vector>

namespace esphome {
namespace tdt_bms {

namespace espbt = esphome::esp32_ble_tracker;

// TDT/WattCycle "smart BMS" protocol (BLE service 0xFFF0), reverse-ported
// from patman15/aiobmsble's tdt_bms.py (Apache-2.0). See that file for the
// canonical reference implementation this is derived from.
static const espbt::ESPBTUUID TDT_SERVICE_UUID = espbt::ESPBTUUID::from_uint16(0xfff0);
static const espbt::ESPBTUUID TDT_UUID_RX = espbt::ESPBTUUID::from_uint16(0xfff1);  // notify
static const espbt::ESPBTUUID TDT_UUID_TX = espbt::ESPBTUUID::from_uint16(0xfff2);  // write
static const espbt::ESPBTUUID TDT_UUID_CFG = espbt::ESPBTUUID::from_uint16(0xfffa);  // unlock gate

static const uint8_t TDT_RSP_HEAD = 0x7E;
// Some hardware revisions of this BMS expect 0x1E as the command head byte
// instead of 0x7E -- aiobmsble's tdt_bms.py tries both and locks in whichever
// one gets a reply. handle_frame_complete_() doesn't check the frame's first
// byte, so a wrong head byte doesn't corrupt parsing; it just gets no notify
// back at all, which is indistinguishable from a dropped write on the wire.
static const uint8_t TDT_ALT_HEAD = 0x1E;
static const uint8_t TDT_TAIL = 0x0D;
static const uint8_t TDT_CMD_VER = 0x00;
static const uint8_t TDT_CELL_POS = 0x08;  // payload offset within a full frame
static const size_t TDT_MIN_FRAME_LEN = 10;
static const uint8_t TDT_CMD_MEASURE = 0x8C;   // voltage/current/SoC/cycles/cells/temps
static const uint8_t TDT_CMD_STATUS = 0x8D;    // mosfet state + problem code
static const uint8_t TDT_CMD_INFO = 0x92;      // device info strings (unused for now)

enum class TdtBmsState {
  IDLE,
  UNLOCKING,       // wrote "HiLink", waiting for write ack
  UNLOCK_READING,  // issued read of the config char, waiting for the value
  SUBSCRIBING,     // registering for notify on the RX characteristic
  ESTABLISHED,     // handshake complete, request/response polling active
};

// One instance per physical battery: owns a BLE client connection, drives the
// TDT unlock handshake, and parses the two read commands (0x8C, 0x8D) needed
// for a full status update.
class TdtBms : public esphome::ble_client::BLEClientNode, public PollingComponent {
 public:
  void setup() override {}
  void update() override;
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_voltage_sensor(sensor::Sensor *s) { voltage_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { current_sensor_ = s; }
  void set_power_sensor(sensor::Sensor *s) { power_sensor_ = s; }
  void set_battery_level_sensor(sensor::Sensor *s) { battery_level_sensor_ = s; }
  void set_cycles_sensor(sensor::Sensor *s) { cycles_sensor_ = s; }
  void set_cell_voltage_min_sensor(sensor::Sensor *s) { cell_voltage_min_sensor_ = s; }
  void set_cell_voltage_max_sensor(sensor::Sensor *s) { cell_voltage_max_sensor_ = s; }
  void set_cell_voltage_delta_sensor(sensor::Sensor *s) { cell_voltage_delta_sensor_ = s; }
  void set_temperature_ambient_sensor(sensor::Sensor *s) { temperature_ambient_sensor_ = s; }
  void set_temperature_mosfet_sensor(sensor::Sensor *s) { temperature_mosfet_sensor_ = s; }
  void set_temperature_cell_sensor(uint8_t idx, sensor::Sensor *s) { temperature_cell_sensors_[idx] = s; }
  void set_problem_code_sensor(sensor::Sensor *s) { problem_code_sensor_ = s; }

  void set_charging_binary_sensor(binary_sensor::BinarySensor *s) { charging_binary_sensor_ = s; }
  void set_discharging_binary_sensor(binary_sensor::BinarySensor *s) { discharging_binary_sensor_ = s; }
  void set_problem_binary_sensor(binary_sensor::BinarySensor *s) { problem_binary_sensor_ = s; }

 protected:
  static const size_t MAX_CELL_TEMPS = 4;

  void start_handshake_();
  void send_command_(uint8_t cmd);
  bool handle_frame_complete_();
  void parse_measure_frame_(const std::vector<uint8_t> &frame);
  void parse_status_frame_(const std::vector<uint8_t> &frame);
  void publish_all_();
  void publish_nan_();

  TdtBmsState state_{TdtBmsState::IDLE};
  uint16_t handle_rx_{0};
  uint16_t handle_tx_{0};
  uint16_t handle_cfg_{0};

  // Which command head byte to use, and whether a reply has ever confirmed it.
  // Until confirmed, an update() cycle that got no reply to the previous poll
  // flips to the other head for the next attempt.
  uint8_t cmd_head_{TDT_RSP_HEAD};
  bool head_confirmed_{false};
  bool got_reply_since_send_{true};

  std::vector<uint8_t> frame_;
  size_t expected_len_{0};

  // Fields parsed from the 0x8C reply, held until the 0x8D reply completes the cycle.
  uint8_t cell_count_{0};
  uint8_t temp_sensor_count_{0};
  float pending_voltage_{NAN};
  float pending_current_{NAN};
  float pending_battery_level_{NAN};
  float pending_cycles_{NAN};
  float pending_cell_voltage_min_{NAN};
  float pending_cell_voltage_max_{NAN};
  float pending_temperature_ambient_{NAN};
  float pending_temperature_mosfet_{NAN};
  float pending_temperature_cell_[MAX_CELL_TEMPS];

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *cycles_sensor_{nullptr};
  sensor::Sensor *cell_voltage_min_sensor_{nullptr};
  sensor::Sensor *cell_voltage_max_sensor_{nullptr};
  sensor::Sensor *cell_voltage_delta_sensor_{nullptr};
  sensor::Sensor *temperature_ambient_sensor_{nullptr};
  sensor::Sensor *temperature_mosfet_sensor_{nullptr};
  sensor::Sensor *temperature_cell_sensors_[MAX_CELL_TEMPS]{nullptr, nullptr, nullptr, nullptr};
  // Raw 16-bit 0x8D problem code. Upstream aiobmsble doesn't decode the bits
  // either, so this is exposed as-is for whoever consumes it to report.
  sensor::Sensor *problem_code_sensor_{nullptr};

  binary_sensor::BinarySensor *charging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *discharging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *problem_binary_sensor_{nullptr};
};

}  // namespace tdt_bms
}  // namespace esphome

#endif
