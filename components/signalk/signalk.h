#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <string>
#include <vector>

namespace esphome {
namespace signalk {

// When an alarm-mapped sensor counts as "raised".
enum class AlarmWhen : uint8_t {
  NONZERO,  // a fault/problem code: any non-zero reading; the code is appended to the message
  MISSING,  // no reading at all: device disconnected, timed out, or out of range
};

// Pushes ESPHome sensor readings to Signal K as delta JSON over UDP. Each
// reading goes out as its own small delta the moment the sensor publishes; a
// NaN reading is sent as a null value so stale data doesn't linger in Signal K.
// Alarms become Signal K notifications (notifications.<path>) only while
// raised, re-sent every resend_interval; when they clear they're removed.
class SignalK : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_target(const std::string &target) { target_ = target; }
  void set_port(uint16_t port) { port_ = port; }
  void set_source(const std::string &source) { source_ = source; }
  void set_resend_interval(uint32_t ms) { resend_interval_ = ms; }

  void add_sensor(sensor::Sensor *sensor, const std::string &path, float multiply, float offset);
  void add_alarm(sensor::Sensor *sensor, const std::string &path, AlarmWhen when, const std::string &message,
                 const std::string &state, uint32_t delay, uint32_t ignore_mask);

 protected:
  struct Alarm {
    sensor::Sensor *sensor;
    std::string path;
    AlarmWhen when;
    std::string message;
    std::string state;
    uint32_t delay;
    uint32_t ignore_mask;         // NONZERO only: bits that don't count as a fault
    uint32_t condition_since{0};  // millis() when the condition started holding, 0 = not holding
    uint8_t clears_pending{0};    // null deltas still to send after the alarm clears
  };
  static const uint8_t CLEAR_RESENDS = 3;

  void send_alarms_();
  void send_alarm_(Alarm &alarm);
  void send_delta_(const std::string &path, const std::string &value_json);

  std::string target_;
  uint16_t port_{4123};
  std::string source_;
  uint32_t resend_interval_{30000};
  int sock_{-1};
  std::vector<Alarm> alarms_;
};

}  // namespace signalk
}  // namespace esphome
