#include "signalk.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>
#include <lwip/sockets.h>

namespace esphome {
namespace signalk {

static const char *const TAG = "signalk";

void SignalK::setup() {
  sock_ = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock_ < 0) {
    ESP_LOGE(TAG, "Could not create UDP socket");
    this->mark_failed();
    return;
  }
  int enable = 1;
  lwip_setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

  if (!alarms_.empty())
    this->set_interval("alarms", resend_interval_, [this]() { this->send_alarms_(); });
}

void SignalK::add_sensor(sensor::Sensor *sensor, const std::string &path, float multiply, float offset) {
  sensor->add_on_state_callback([this, path, multiply, offset](float value) {
    if (std::isnan(value)) {
      this->send_delta_(path, "null");
      return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", value * multiply + offset);
    this->send_delta_(path, buf);
  });
}

void SignalK::add_alarm(sensor::Sensor *sensor, const std::string &path, AlarmWhen when, const std::string &message,
                        const std::string &state, uint32_t delay, uint32_t ignore_mask) {
  alarms_.push_back(Alarm{sensor, "notifications." + path, when, message, state, delay, ignore_mask});
  // Also evaluate immediately when the reading changes, so a fault shows up
  // within one sensor update instead of waiting for the next resend.
  size_t idx = alarms_.size() - 1;
  sensor->add_on_state_callback([this, idx](float) { this->send_alarm_(alarms_[idx]); });
}

void SignalK::send_alarms_() {
  for (auto &alarm : alarms_)
    this->send_alarm_(alarm);
}

void SignalK::send_alarm_(Alarm &alarm) {
  float value = alarm.sensor->state;
  bool condition;
  std::string message = alarm.message;
  switch (alarm.when) {
    case AlarmWhen::MISSING:
      condition = std::isnan(value);
      break;
    case AlarmWhen::NONZERO:
      // Bits in ignore_mask are status flags, not faults (e.g. a BMS's "full"
      // bit), so only the remaining bits count; the full code goes in the message.
      condition = !std::isnan(value) && (static_cast<uint32_t>(value) & ~alarm.ignore_mask) != 0;
      if (condition) {
        char code[24];
        snprintf(code, sizeof(code), " (code 0x%04X)", static_cast<unsigned>(value));
        message += code;
      }
      break;
    default:
      return;
  }

  // The condition has to hold for `delay` before it's raised. Sensors start
  // out NaN at boot, and a BLE reconnect blips NaN, so without this every
  // reboot would briefly raise every MISSING alarm.
  uint32_t now = millis();
  if (!condition) {
    alarm.condition_since = 0;
  } else if (alarm.condition_since == 0) {
    alarm.condition_since = now == 0 ? 1 : now;
  }
  bool raised = condition && now - alarm.condition_since >= alarm.delay;

  if (raised) {
    alarm.clears_pending = CLEAR_RESENDS;
    std::string json = "{\"state\":\"";
    json += alarm.state;
    json += "\",\"method\":[\"visual\",\"sound\"],\"message\":\"";
    json += message;
    json += "\"}";
    this->send_delta_(alarm.path, json);
  } else if (alarm.clears_pending > 0) {
    // A cleared alarm is removed (null), not left behind as a "normal"
    // notification -- chartplotters like AvNav show any notification that
    // exists. Sent a few times since UDP can drop it; never sent for an
    // alarm that was never raised.
    alarm.clears_pending--;
    this->send_delta_(alarm.path, "null");
  }
}

void SignalK::send_delta_(const std::string &path, const std::string &value_json) {
  if (sock_ < 0)
    return;
  // Signal K replaces `label` with the data connection's id; `src` survives,
  // so $source reads e.g. "esphome.defiant-bms-bank1".
  std::string delta = "{\"updates\":[{\"source\":{\"label\":\"";
  delta += source_;
  delta += "\",\"src\":\"";
  delta += source_;
  delta += "\"},\"values\":[{\"path\":\"";
  delta += path;
  delta += "\",\"value\":";
  delta += value_json;
  delta += "}]}]}";

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = inet_addr(target_.c_str());
  // Fire and forget: if WiFi is down this just fails and the next reading tries again.
  lwip_sendto(sock_, delta.data(), delta.size(), 0, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
}

void SignalK::dump_config() {
  ESP_LOGCONFIG(TAG, "Signal K UDP:");
  ESP_LOGCONFIG(TAG, "  Target: %s:%u", target_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Source: %s", source_.c_str());
  ESP_LOGCONFIG(TAG, "  Alarms: %u (resend every %ums)", (unsigned) alarms_.size(), (unsigned) resend_interval_);
}

}  // namespace signalk
}  // namespace esphome
