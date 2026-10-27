#pragma once

#include <string>
#include <vector>

#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_RF_SNIFFER_RSSI
#include "esphome/components/cc1101/cc1101.h"
#endif
#ifdef USE_RF_SNIFFER_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include "sniffer_core.h"

namespace esphome {
namespace rf_sniffer {

/// Listens to a remote_receiver, decodes every frame with all known decoders and
/// logs one line per transmission with its repeat count.
class RfSniffer : public PollingComponent, public remote_base::RemoteReceiverListener {
 public:
  RfSniffer();
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  bool on_receive(remote_base::RemoteReceiveData data) override;

  // Runtime settings, usually set from the web UI entities.
  void set_raw_mode(RawMode mode) { this->raw_mode_ = mode; }
  void set_raw_mode_index(uint8_t index) { this->raw_mode_ = static_cast<RawMode>(index > 2 ? 0 : index); }
  void set_min_pulses(uint32_t min_pulses) { this->min_pulses_ = min_pulses; }
  void set_max_repeat_wait(uint32_t ms) { this->max_repeat_wait_ms_ = ms; }
  /// remote_receiver receive_symbols, to mark captures that filled the buffer.
  void set_receive_symbols(uint32_t symbols) { this->receive_symbols_ = symbols; }
  void set_decoder_visible(Decoder decoder, bool visible);
  void set_oregon_timing(const ManchesterDecoder::Timing &timing) { this->oregon_.set_timing(timing); }
  void set_oregon_verify_bit_pairs(bool verify) { this->oregon_.set_verify_bit_pairs(verify); }
  /// Zeroes the counters since boot and publishes them at once.
  void reset_statistics();

#ifdef USE_RF_SNIFFER_RSSI
  void set_radio(cc1101::CC1101Component *radio) { this->radio_ = radio; }
  /// The receiver data pin (GDO2), watched for the start of frames.
  void set_activity_pin(InternalGPIOPin *pin) { this->activity_pin_ = pin; }
#endif
#ifdef USE_RF_SNIFFER_TIME
  void set_time(time::RealTimeClock *clock) { this->time_ = clock; }
#endif
#ifdef USE_SENSOR
  void set_frames_sensor(sensor::Sensor *s) { this->frames_sensor_ = s; }
  void set_decoded_sensor(sensor::Sensor *s) { this->decoded_sensor_ = s; }
  void set_unknown_sensor(sensor::Sensor *s) { this->unknown_sensor_ = s; }
  void set_noise_per_minute_sensor(sensor::Sensor *s) { this->noise_per_minute_sensor_ = s; }
  void set_noise_floor_sensor(sensor::Sensor *s) { this->noise_floor_sensor_ = s; }
  void set_oregon_rejected_sensor(sensor::Sensor *s) { this->oregon_rejected_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_per_protocol_text_sensor(text_sensor::TextSensor *s) { this->per_protocol_sensor_ = s; }
#endif

  /// Decoders compiled into this build (dev only ones depend on the ESPHome version).
  static bool decoder_available(Decoder decoder);
  /// Decoders without a checksum: only run while visible, and a line needs two
  /// identical copies before it is logged.
  static bool decoder_is_weak(Decoder decoder) { return decoder == Decoder::DRAYTON || decoder == Decoder::RC_SWITCH; }

 protected:
  uint64_t now_ms_();
  bool decode_builtin_(const remote_base::RemoteReceiveData &data, Decoder &decoder, Decoded &out);
  void log_raw_(const std::vector<int32_t> &raw, uint64_t start_ms, const std::string &signal);
  /// Noise floor of the current interval, or the last one while the current has no samples yet.
  bool noise_floor_(float &dbm);
  void log_line_(const Line &line);
  std::string timestamp_(uint64_t uptime_ms);
  bool clock_valid_();
  void sample_rssi_();
  bool read_rssi_raw_(uint8_t &raw);
  void check_clock_();

  OregonSniffer oregon_;
  RepeatAggregator aggregator_;
  RssiTracker rssi_;
  std::vector<Line> finished_;
  bool visible_[DECODER_COUNT];
  RawMode raw_mode_{RawMode::OFF};
  uint32_t min_pulses_{60};
  uint32_t max_repeat_wait_ms_{500};
  uint32_t receive_symbols_{0};
  bool has_last_floor_{false};
  float last_floor_dbm_{0};

  // 64 bit uptime from the 32 bit millis().
  uint32_t last_millis_{0};
  uint64_t millis_high_{0};
  uint64_t last_frame_ms_{0};
  uint64_t last_frame_end_ms_{0};

  // Statistics, counted since boot unless noted.
  uint32_t frames_{0};
  uint32_t decoded_{0};
  uint32_t unknown_{0};
  uint32_t oregon_rejected_{0};
  uint32_t noise_interval_{0};  // since the last update()
  uint64_t interval_start_ms_{0};
  uint32_t per_decoder_[DECODER_COUNT]{};

  // Clock warnings.
  uint64_t last_clock_warning_ms_{0};
  uint64_t last_sync_ms_{0};
  bool synced_once_{false};

#ifdef USE_RF_SNIFFER_RSSI
  static void gpio_intr_(RfSniffer *arg);
  cc1101::CC1101Component *radio_{nullptr};
  InternalGPIOPin *activity_pin_{nullptr};
  ISRInternalGPIOPin activity_isr_pin_;
  // Written by the interrupt on every edge of the data pin.
  volatile uint32_t last_edge_us_{0};
  volatile bool frame_started_{false};
  uint32_t last_rssi_us_{0};
  uint32_t last_floor_us_{0};
  uint8_t frame_reads_{0};
  // Runs the loop continuously only while a frame is being received.
  HighFrequencyLoopRequester high_freq_;
#endif
#ifdef USE_RF_SNIFFER_TIME
  time::RealTimeClock *time_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *frames_sensor_{nullptr};
  sensor::Sensor *decoded_sensor_{nullptr};
  sensor::Sensor *unknown_sensor_{nullptr};
  sensor::Sensor *noise_per_minute_sensor_{nullptr};
  sensor::Sensor *noise_floor_sensor_{nullptr};
  sensor::Sensor *oregon_rejected_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *per_protocol_sensor_{nullptr};
#endif
};

}  // namespace rf_sniffer
}  // namespace esphome
