#include "tdt_bms.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef USE_ESP32

namespace esphome {
namespace tdt_bms {

static const char *const TAG = "tdt_bms";

// CRC-16/MODBUS: poly 0xA001, init 0xFFFF. Matches aiobmsble's crc_modbus().
static uint16_t crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 0x1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// Builds a read-command frame: [head, ver, 0x01, 0x03, 0x00, cmd, len_hi, len_lo, crc_hi, crc_lo, tail].
// All three commands we use (0x8C, 0x8D, 0x92) carry no request payload.
static std::vector<uint8_t> build_command_frame(uint8_t cmd, uint8_t head) {
  std::vector<uint8_t> frame = {head, TDT_CMD_VER, 0x01, 0x03, 0x00, cmd, 0x00, 0x00};
  uint16_t crc = crc16_modbus(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc >> 8));
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(TDT_TAIL);
  return frame;
}

void TdtBms::send_command_(uint8_t cmd) {
  if (handle_tx_ == 0) {
    return;
  }
  if (!got_reply_since_send_ && !head_confirmed_) {
    cmd_head_ = (cmd_head_ == TDT_RSP_HEAD) ? TDT_ALT_HEAD : TDT_RSP_HEAD;
    ESP_LOGD(TAG, "[%s] no reply to last poll, trying command head 0x%02X", this->parent_->address_str(), cmd_head_);
  }
  got_reply_since_send_ = false;
  auto frame = build_command_frame(cmd, cmd_head_);
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(), handle_tx_,
                                         frame.size(), frame.data(), ESP_GATT_WRITE_TYPE_RSP,
                                         ESP_GATT_AUTH_REQ_NONE);
  if (status) {
    ESP_LOGW(TAG, "[%s] write cmd 0x%02X failed, status=%d", this->parent_->address_str(), cmd, status);
  }
}

void TdtBms::start_handshake_() {
  auto *rx = this->parent_->get_characteristic(TDT_SERVICE_UUID, TDT_UUID_RX);
  auto *tx = this->parent_->get_characteristic(TDT_SERVICE_UUID, TDT_UUID_TX);
  auto *cfg = this->parent_->get_characteristic(TDT_SERVICE_UUID, TDT_UUID_CFG);
  if (rx == nullptr || tx == nullptr || cfg == nullptr) {
    ESP_LOGW(TAG, "[%s] missing FFF1/FFF2/FFFA characteristic, not a TDT/WattCycle BMS?",
             this->parent_->address_str());
    this->parent()->set_enabled(false);
    return;
  }
  handle_rx_ = rx->handle;
  handle_tx_ = tx->handle;
  handle_cfg_ = cfg->handle;

  static const uint8_t UNLOCK_MAGIC[] = {'H', 'i', 'L', 'i', 'n', 'k'};
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(), handle_cfg_,
                                         sizeof(UNLOCK_MAGIC), const_cast<uint8_t *>(UNLOCK_MAGIC),
                                         ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status) {
    ESP_LOGW(TAG, "[%s] unlock write failed, status=%d", this->parent_->address_str(), status);
    return;
  }
  state_ = TdtBmsState::UNLOCKING;
}

void TdtBms::update() {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGD(TAG, "[%s] cannot poll, not connected", this->parent_->address_str());
    return;
  }
  frame_.clear();
  expected_len_ = 0;
  this->send_command_(TDT_CMD_MEASURE);
}

// Returns true once `frame_` holds a complete, checksum-valid frame ready to dispatch.
bool TdtBms::handle_frame_complete_() {
  if (frame_.size() < std::max(TDT_MIN_FRAME_LEN, expected_len_)) {
    return false;
  }
  if (frame_.back() != TDT_TAIL) {
    ESP_LOGD(TAG, "[%s] frame end incorrect", this->parent_->address_str());
    return false;
  }
  if (frame_[1] != 0x00 && frame_[1] != 0x04) {
    ESP_LOGD(TAG, "[%s] unknown frame version V%u", this->parent_->address_str(), frame_[1]);
    return false;
  }
  if (frame_[4] != 0x00) {
    ESP_LOGD(TAG, "[%s] BMS reported error code 0x%02X", this->parent_->address_str(), frame_[4]);
    return false;
  }
  size_t n = frame_.size();
  uint16_t calc_crc = crc16_modbus(frame_.data(), n - 3);
  uint16_t exp_crc = (static_cast<uint16_t>(frame_[n - 3]) << 8) | frame_[n - 2];
  if (calc_crc != exp_crc) {
    ESP_LOGW(TAG, "[%s] invalid checksum 0x%04X != 0x%04X", this->parent_->address_str(), exp_crc, calc_crc);
    return false;
  }
  return true;
}

void TdtBms::parse_measure_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() <= TDT_CELL_POS) {
    ESP_LOGW(TAG, "[%s] measure frame too short (%u bytes, need > %u)", this->parent_->address_str(),
             (unsigned) frame.size(), TDT_CELL_POS);
    return;
  }
  cell_count_ = frame[TDT_CELL_POS];
  size_t temp_count_pos = TDT_CELL_POS + cell_count_ * 2 + 1;
  if (temp_count_pos >= frame.size()) {
    return;
  }
  temp_sensor_count_ = frame[temp_count_pos];

  // Cell voltages: 2 bytes big-endian each, raw millivolts.
  pending_cell_voltage_min_ = NAN;
  pending_cell_voltage_max_ = NAN;
  size_t cell_start = TDT_CELL_POS + 1;
  for (uint8_t i = 0; i < cell_count_; i++) {
    size_t off = cell_start + i * 2;
    if (off + 1 >= frame.size())
      break;
    float v = ((static_cast<uint16_t>(frame[off]) << 8) | frame[off + 1]) / 1000.0f;
    if (std::isnan(pending_cell_voltage_min_) || v < pending_cell_voltage_min_)
      pending_cell_voltage_min_ = v;
    if (std::isnan(pending_cell_voltage_max_) || v > pending_cell_voltage_max_)
      pending_cell_voltage_max_ = v;
  }

  // Temperatures: [ambient, mosfet, cell_1, cell_2, ...], decikelvin - 2731, /10 => degC.
  for (auto &t : pending_temperature_cell_)
    t = NAN;
  pending_temperature_ambient_ = NAN;
  pending_temperature_mosfet_ = NAN;
  size_t temp_start = TDT_CELL_POS + cell_count_ * 2 + 2;
  for (uint8_t i = 0; i < temp_sensor_count_; i++) {
    size_t off = temp_start + i * 2;
    if (off + 1 >= frame.size())
      break;
    uint16_t raw = (static_cast<uint16_t>(frame[off]) << 8) | frame[off + 1];
    float temp_c = (static_cast<float>(raw) - 2731.0f) / 10.0f;
    if (i == 0) {
      pending_temperature_ambient_ = temp_c;
    } else if (i == 1) {
      pending_temperature_mosfet_ = temp_c;
    } else if (i - 2 < MAX_CELL_TEMPS) {
      pending_temperature_cell_[i - 2] = temp_c;
    }
  }

  // Fixed-offset fields: current, voltage, cycle_charge (unused), cycles, battery_level.
  uint16_t idx = static_cast<uint16_t>(cell_count_) + temp_sensor_count_;
  size_t start = TDT_CELL_POS + idx * 2 + 2;
  auto u16_at = [&](size_t pos) -> int32_t {
    if (pos + 1 >= frame.size())
      return -1;
    return (static_cast<uint16_t>(frame[pos]) << 8) | frame[pos + 1];
  };

  int32_t current_raw = u16_at(start + 0);
  if (current_raw >= 0) {
    bool negative = current_raw & 0x8000;
    float mag = (current_raw & 0x3FFF) / 10.0f;
    pending_current_ = negative ? -mag : mag;
  }
  int32_t voltage_raw = u16_at(start + 2);
  if (voltage_raw >= 0)
    pending_voltage_ = voltage_raw / 100.0f;
  int32_t cycles_raw = u16_at(start + 8);
  if (cycles_raw >= 0)
    pending_cycles_ = static_cast<float>(cycles_raw);
  int32_t soc_raw = u16_at(start + 12);
  if (soc_raw >= 0)
    pending_battery_level_ = static_cast<float>(soc_raw);
}

void TdtBms::parse_status_frame_(const std::vector<uint8_t> &frame) {
  // NOTE: offsets here use `idx` directly (not idx*2), matching the upstream
  // aiobmsble tdt_bms.py layout for the 0x8D reply -- it is not the same
  // arithmetic as the 0x8C parser above, but it is what the firmware sends.
  uint16_t idx = static_cast<uint16_t>(cell_count_) + temp_sensor_count_;
  size_t problem_pos = TDT_CELL_POS + idx + 6;
  size_t mosfet_pos = TDT_CELL_POS + idx + 8;
  bool problem = false;
  float problem_code_value = NAN;
  if (problem_pos + 1 < frame.size()) {
    uint16_t problem_code = (static_cast<uint16_t>(frame[problem_pos]) << 8) | frame[problem_pos + 1];
    problem = problem_code != 0;
    problem_code_value = static_cast<float>(problem_code);
  }
  bool charging = false;
  bool discharging = false;
  if (mosfet_pos < frame.size()) {
    uint8_t mosfets = frame[mosfet_pos];
    charging = mosfets & 0x04;
    discharging = mosfets & 0x02;
  }
  if (charging_binary_sensor_ != nullptr)
    charging_binary_sensor_->publish_state(charging);
  if (discharging_binary_sensor_ != nullptr)
    discharging_binary_sensor_->publish_state(discharging);
  if (problem_binary_sensor_ != nullptr)
    problem_binary_sensor_->publish_state(problem);
  if (problem_code_sensor_ != nullptr)
    problem_code_sensor_->publish_state(problem_code_value);
}

void TdtBms::publish_all_() {
  if (voltage_sensor_ != nullptr)
    voltage_sensor_->publish_state(pending_voltage_);
  if (current_sensor_ != nullptr)
    current_sensor_->publish_state(pending_current_);
  if (power_sensor_ != nullptr && !std::isnan(pending_voltage_) && !std::isnan(pending_current_))
    power_sensor_->publish_state(pending_voltage_ * pending_current_);
  if (battery_level_sensor_ != nullptr)
    battery_level_sensor_->publish_state(pending_battery_level_);
  if (cycles_sensor_ != nullptr)
    cycles_sensor_->publish_state(pending_cycles_);
  if (cell_voltage_min_sensor_ != nullptr)
    cell_voltage_min_sensor_->publish_state(pending_cell_voltage_min_);
  if (cell_voltage_max_sensor_ != nullptr)
    cell_voltage_max_sensor_->publish_state(pending_cell_voltage_max_);
  if (cell_voltage_delta_sensor_ != nullptr && !std::isnan(pending_cell_voltage_min_) &&
      !std::isnan(pending_cell_voltage_max_))
    cell_voltage_delta_sensor_->publish_state(pending_cell_voltage_max_ - pending_cell_voltage_min_);
  if (temperature_ambient_sensor_ != nullptr)
    temperature_ambient_sensor_->publish_state(pending_temperature_ambient_);
  if (temperature_mosfet_sensor_ != nullptr)
    temperature_mosfet_sensor_->publish_state(pending_temperature_mosfet_);
  for (size_t i = 0; i < MAX_CELL_TEMPS; i++) {
    if (temperature_cell_sensors_[i] != nullptr)
      temperature_cell_sensors_[i]->publish_state(pending_temperature_cell_[i]);
  }
}

void TdtBms::publish_nan_() {
  if (voltage_sensor_ != nullptr)
    voltage_sensor_->publish_state(NAN);
  if (current_sensor_ != nullptr)
    current_sensor_->publish_state(NAN);
  if (power_sensor_ != nullptr)
    power_sensor_->publish_state(NAN);
  if (battery_level_sensor_ != nullptr)
    battery_level_sensor_->publish_state(NAN);
  if (cycles_sensor_ != nullptr)
    cycles_sensor_->publish_state(NAN);
  if (cell_voltage_min_sensor_ != nullptr)
    cell_voltage_min_sensor_->publish_state(NAN);
  if (cell_voltage_max_sensor_ != nullptr)
    cell_voltage_max_sensor_->publish_state(NAN);
  if (cell_voltage_delta_sensor_ != nullptr)
    cell_voltage_delta_sensor_->publish_state(NAN);
  if (temperature_ambient_sensor_ != nullptr)
    temperature_ambient_sensor_->publish_state(NAN);
  if (temperature_mosfet_sensor_ != nullptr)
    temperature_mosfet_sensor_->publish_state(NAN);
  for (auto *s : temperature_cell_sensors_) {
    if (s != nullptr)
      s->publish_state(NAN);
  }
  if (problem_code_sensor_ != nullptr)
    problem_code_sensor_->publish_state(NAN);
}

void TdtBms::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                 esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT: {
      state_ = TdtBmsState::IDLE;
      this->node_state = espbt::ClientState::IDLE;
      handle_rx_ = handle_tx_ = handle_cfg_ = 0;
      frame_.clear();
      expected_len_ = 0;
      this->publish_nan_();
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      this->start_handshake_();
      break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[%s] write to handle 0x%04X failed, status=%d", this->parent_->address_str(),
                 param->write.handle, param->write.status);
        break;
      }
      if (state_ == TdtBmsState::UNLOCKING && param->write.handle == handle_cfg_) {
        auto status = esp_ble_gattc_read_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(), handle_cfg_,
                                              ESP_GATT_AUTH_REQ_NONE);
        if (status) {
          ESP_LOGW(TAG, "[%s] unlock read failed, status=%d", this->parent_->address_str(), status);
          break;
        }
        state_ = TdtBmsState::UNLOCK_READING;
      }
      break;
    }
    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.conn_id != this->parent_->get_conn_id())
        break;
      if (state_ == TdtBmsState::UNLOCK_READING && param->read.handle == handle_cfg_) {
        bool unlocked = param->read.status == ESP_GATT_OK && param->read.value_len > 0 && param->read.value[0] == 0x01;
        if (!unlocked) {
          ESP_LOGW(TAG, "[%s] BMS did not unlock (status=%d)", this->parent_->address_str(),
                   param->read.status);
          break;
        }
        auto status =
            esp_ble_gattc_register_for_notify(this->parent_->get_gattc_if(), this->parent_->get_remote_bda(), handle_rx_);
        if (status) {
          ESP_LOGW(TAG, "[%s] register_for_notify failed, status=%d", this->parent_->address_str(), status);
          break;
        }
        state_ = TdtBmsState::SUBSCRIBING;
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.handle != handle_rx_)
        break;
      state_ = TdtBmsState::ESTABLISHED;
      this->node_state = espbt::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "[%s] TDT BMS handshake complete", this->parent_->address_str());
      this->update();
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != handle_rx_)
        break;
      const uint8_t *data = param->notify.value;
      size_t len = param->notify.value_len;
      if (len > TDT_MIN_FRAME_LEN && data[0] == TDT_RSP_HEAD && frame_.size() >= expected_len_) {
        expected_len_ = TDT_MIN_FRAME_LEN + ((static_cast<uint16_t>(data[6]) << 8) | data[7]);
        frame_.clear();
      }
      frame_.insert(frame_.end(), data, data + len);

      if (!this->handle_frame_complete_())
        break;
      got_reply_since_send_ = true;
      head_confirmed_ = true;

      uint8_t reply = frame_[5];
      if (reply == TDT_CMD_MEASURE) {
        this->parse_measure_frame_(frame_);
        frame_.clear();
        expected_len_ = 0;
        this->send_command_(TDT_CMD_STATUS);
      } else if (reply == TDT_CMD_STATUS) {
        this->parse_status_frame_(frame_);
        frame_.clear();
        expected_len_ = 0;
        this->publish_all_();
      } else {
        frame_.clear();
        expected_len_ = 0;
      }
      break;
    }
    default:
      break;
  }
}

void TdtBms::dump_config() {
  ESP_LOGCONFIG(TAG, "TDT/WattCycle BMS:");
  LOG_SENSOR("  ", "Voltage", voltage_sensor_);
  LOG_SENSOR("  ", "Current", current_sensor_);
  LOG_SENSOR("  ", "Power", power_sensor_);
  LOG_SENSOR("  ", "Battery Level", battery_level_sensor_);
  LOG_SENSOR("  ", "Cycles", cycles_sensor_);
  LOG_SENSOR("  ", "Cell Voltage Min", cell_voltage_min_sensor_);
  LOG_SENSOR("  ", "Cell Voltage Max", cell_voltage_max_sensor_);
  LOG_SENSOR("  ", "Cell Voltage Delta", cell_voltage_delta_sensor_);
  LOG_SENSOR("  ", "Temperature Ambient", temperature_ambient_sensor_);
  LOG_SENSOR("  ", "Temperature Mosfet", temperature_mosfet_sensor_);
  LOG_SENSOR("  ", "Problem Code", problem_code_sensor_);
  LOG_BINARY_SENSOR("  ", "Charging", charging_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Discharging", discharging_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Problem", problem_binary_sensor_);
}

}  // namespace tdt_bms
}  // namespace esphome

#endif
