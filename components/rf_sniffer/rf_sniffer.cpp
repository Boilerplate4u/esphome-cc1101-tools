#include "rf_sniffer.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <sys/time.h>

#include "esphome/components/remote_base/brennenstuhl_protocol.h"
#include "esphome/components/remote_base/byronsx_protocol.h"
#include "esphome/components/remote_base/dooya_protocol.h"
#include "esphome/components/remote_base/drayton_protocol.h"
#include "esphome/components/remote_base/keeloq_protocol.h"
#include "esphome/components/remote_base/nexa_protocol.h"
#include "esphome/components/remote_base/rc_switch_protocol.h"
#ifdef RF_SNIFFER_HAS_NEXUS
#include "esphome/components/remote_base/nexus_protocol.h"
#endif
#ifdef RF_SNIFFER_HAS_AOK
#include "esphome/components/remote_base/aok_protocol.h"
#endif
#ifdef RF_SNIFFER_HAS_CARDIN_S438
#include "esphome/components/remote_base/cardin_s438_protocol.h"
#endif
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rf_sniffer {

static const char *const TAG = "rf_sniffer";
static const char *const TAG_RAW = "rf_raw";

static const size_t RAW_VALUES_PER_LINE = 32;
static const uint32_t RSSI_INTERVAL_US = 1000;
static const uint64_t CLOCK_WARNING_INTERVAL_MS = 60 * 1000;
static const uint64_t CLOCK_STALE_MS = 60 * 60 * 1000;
// CC1101 status register RSSI, read as burst (0x40) + read (0x80).
static const uint8_t CC1101_RSSI_STATUS = 0x34 | 0xC0;

static std::string printf_string(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static std::string printf_string(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n < 0)
    return std::string();
  return std::string(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
}

bool RfSniffer::decoder_available(Decoder decoder) {
  switch (decoder) {
    case Decoder::NEXUS:
#ifdef RF_SNIFFER_HAS_NEXUS
      return true;
#else
      return false;
#endif
    case Decoder::AOK:
#ifdef RF_SNIFFER_HAS_AOK
      return true;
#else
      return false;
#endif
    case Decoder::CARDIN_S438:
#ifdef RF_SNIFFER_HAS_CARDIN_S438
      return true;
#else
      return false;
#endif
    case Decoder::COUNT:
      return false;
    default:
      return true;
  }
}

void RfSniffer::set_decoder_visible(Decoder decoder, bool visible) {
  const size_t i = static_cast<size_t>(decoder);
  if (i < DECODER_COUNT)
    this->visible_[i] = visible;
}

uint64_t RfSniffer::now_ms_() {
  const uint32_t now = millis();
  if (now < this->last_millis_)
    this->millis_high_ += uint64_t(1) << 32;
  this->last_millis_ = now;
  return this->millis_high_ | now;
}

RfSniffer::RfSniffer() {
  // In the constructor: the web UI switches may restore their state before setup().
  for (auto &v : this->visible_)
    v = true;
}

void RfSniffer::setup() {
  this->interval_start_ms_ = this->now_ms_();
#ifdef USE_RF_SNIFFER_RSSI
  if (this->radio_ != nullptr)
    this->high_freq_.start();  // RSSI is sampled about every millisecond
#endif
#ifdef USE_RF_SNIFFER_TIME
  if (this->time_ != nullptr) {
    this->time_->add_on_time_sync_callback([this]() {
      this->last_sync_ms_ = this->now_ms_();
      if (!this->synced_once_)
        ESP_LOGI(TAG, "Clock synchronised, timestamps show local time");
      this->synced_once_ = true;
    });
  }
#endif
}

bool RfSniffer::clock_valid_() {
#ifdef USE_RF_SNIFFER_TIME
  return this->time_ != nullptr && this->time_->now().is_valid();
#else
  return false;
#endif
}

void RfSniffer::check_clock_() {
  const uint64_t now = this->now_ms_();
  if (this->last_clock_warning_ms_ != 0 && now - this->last_clock_warning_ms_ < CLOCK_WARNING_INTERVAL_MS)
    return;
#ifdef USE_RF_SNIFFER_TIME
  if (this->time_ == nullptr) {
    if (this->last_clock_warning_ms_ == 0)
      ESP_LOGW(TAG, "No time source configured; timestamps show uptime");
    this->last_clock_warning_ms_ = now;
    return;
  }
  if (!this->clock_valid_()) {
    ESP_LOGW(TAG, "Clock not synchronised; timestamps show uptime until it is");
    this->last_clock_warning_ms_ = now;
    return;
  }
  if (this->synced_once_ && now - this->last_sync_ms_ > CLOCK_STALE_MS) {
    ESP_LOGW(TAG, "Clock not synchronised for %" PRIu64 " min; timestamps may drift",
             (now - this->last_sync_ms_) / 60000);
    this->last_clock_warning_ms_ = now;
  }
#else
  if (this->last_clock_warning_ms_ == 0)
    ESP_LOGW(TAG, "No time source configured; timestamps show uptime");
  this->last_clock_warning_ms_ = now;
#endif
}

std::string RfSniffer::timestamp_(uint64_t uptime_ms) {
  if (!this->clock_valid_())
    return format_uptime(uptime_ms);
  // Wall clock now minus the age of the event, in local time.
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const uint64_t wall_now_ms = uint64_t(tv.tv_sec) * 1000 + uint64_t(tv.tv_usec) / 1000;
  const uint64_t age_ms = this->now_ms_() - uptime_ms;
  const uint64_t wall_ms = wall_now_ms - age_ms;
  const time_t secs = static_cast<time_t>(wall_ms / 1000);
  struct tm local;
  localtime_r(&secs, &local);
  return format_wall_time(local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(wall_ms % 1000));
}

void RfSniffer::read_rssi_() {
#ifdef USE_RF_SNIFFER_RSSI
  if (this->radio_ == nullptr)
    return;
  const uint32_t now_us = micros();
  if (now_us - this->last_rssi_us_ < RSSI_INTERVAL_US)
    return;
  this->last_rssi_us_ = now_us;
  // CC1101 errata: status registers can change while being read; read until two agree.
  uint8_t previous = 0;
  for (int attempt = 0; attempt < 4; attempt++) {
    this->radio_->enable();
    this->radio_->write_byte(CC1101_RSSI_STATUS);
    const uint8_t value = this->radio_->transfer_byte(0);
    this->radio_->disable();
    if (attempt > 0 && value == previous) {
      this->rssi_.add(this->now_ms_(), value);
      return;
    }
    previous = value;
  }
#endif
}

void RfSniffer::loop() {
  this->read_rssi_();
  this->check_clock_();
  this->finished_.clear();
  this->aggregator_.poll(this->now_ms_(), this->max_repeat_wait_ms_, this->finished_);
  for (const auto &line : this->finished_)
    this->log_line_(line);
}

bool RfSniffer::decode_builtin_(const remote_base::RemoteReceiveData &data, Decoder &decoder, Decoded &out) {
  using namespace remote_base;
  {
    auto r = NexaProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::NEXA;
      const char *state = r->state == 1 ? "on" : (r->state == 0 ? "off" : "dim");
      out.sender = printf_string("%07" PRIX32 ":%u:%u", r->device, r->group, r->channel);
      out.fields = printf_string("device=%07" PRIX32 " group=%u channel=%u state=%s level=%u", r->device, r->group,
                                 r->channel, state, r->level);
      return true;
    }
  }
  {
    auto r = KeeloqProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::KEELOQ;
      out.sender = printf_string("%07" PRIX32, r->address);
      out.fields = printf_string("address=%07" PRIX32 " command=%02X encrypted=%08" PRIX32 " battery=%s", r->address,
                                 r->command, r->encrypted, r->vlow ? "low" : "ok");
      return true;
    }
  }
  {
    auto r = DooyaProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::DOOYA;
      out.sender = printf_string("%06" PRIX32 ":%u", r->id, r->channel);
      out.fields =
          printf_string("id=%06" PRIX32 " channel=%u button=%u check=%u", r->id, r->channel, r->button, r->check);
      return true;
    }
  }
  {
    auto r = DraytonProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::DRAYTON;
      out.sender = printf_string("%04X:%u", r->address, r->channel);
      out.fields = printf_string("address=%04X channel=%u command=%u", r->address, r->channel, r->command);
      return true;
    }
  }
  {
    auto r = ByronSXProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::BYRONSX;
      out.sender = printf_string("%02X", r->address);
      out.fields = printf_string("address=%02X command=%u", r->address, r->command);
      return true;
    }
  }
  {
    auto r = BrennenstuhlProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::BRENNENSTUHL;
      out.sender = printf_string("%06" PRIX32, r->code);
      out.fields = printf_string("code=%06" PRIX32, r->code);
      return true;
    }
  }
#ifdef RF_SNIFFER_HAS_NEXUS
  {
    auto r = NexusProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::NEXUS;
      out.sender = printf_string("%02X:%u", r->address, r->channel);
      out.fields =
          printf_string("address=%02X channel=%u temp=%.1f hum=%u battery_level=%u force_update=%u", r->address,
                        r->channel, r->temperature, r->humidity, r->battery_level ? 1 : 0, r->force_update ? 1 : 0);
      return true;
    }
  }
#endif
#ifdef RF_SNIFFER_HAS_AOK
  {
    auto r = AOKProtocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::AOK;
      out.sender = printf_string("%06" PRIX32, r->remote_id);
      out.fields =
          printf_string("remote_id=%06" PRIX32 " channels=%04X command=%02X", r->remote_id, r->channels, r->command);
      return true;
    }
  }
#endif
#ifdef RF_SNIFFER_HAS_CARDIN_S438
  {
    auto r = CardinS438Protocol().decode(data);
    if (r.has_value()) {
      decoder = Decoder::CARDIN_S438;
      out.sender = printf_string("%03X", r->code);
      out.fields = printf_string("code=%03X channel=%u seed=%02X", r->code, r->channel, r->seed);
      return true;
    }
  }
#endif
  // Generic and last: only when no specific protocol matched.
  for (size_t i = 1; i < std::size(RC_SWITCH_PROTOCOLS); i++) {
    RemoteReceiveData src = data;
    src.reset();
    uint64_t code = 0;
    uint8_t nbits = 0;
    const RCSwitchBase protocol = rc_switch_protocol(static_cast<uint8_t>(i));
    if (protocol.decode(src, &code, &nbits) && nbits >= 3) {
      decoder = Decoder::RC_SWITCH;
      out.sender = printf_string("%u:%" PRIX64, static_cast<unsigned>(i), code);
      out.fields = printf_string("protocol=%u bits=%u code=%" PRIX64, static_cast<unsigned>(i), nbits, code);
      return true;
    }
  }
  return false;
}

bool RfSniffer::on_receive(remote_base::RemoteReceiveData data) {
  const auto &raw = data.get_raw_data();
  const uint64_t end_ms = this->now_ms_();
  const uint64_t duration_ms = capture_duration_us(raw) / 1000;
  const uint64_t start_ms = end_ms > duration_ms ? end_ms - duration_ms : 0;
  this->frames_++;

  int16_t rssi_dbm = 0;
  const bool has_rssi = this->rssi_.peak(start_ms, end_ms, rssi_dbm);

  std::vector<Decoded> found;
  Decoder decoder = Decoder::OREGON;
  bool claimed = this->oregon_.decode(raw, found) > 0;
  if (!claimed) {
    Decoded d;
    if (this->decode_builtin_(data, decoder, d)) {
      d.decoder = decoder;
      found.push_back(d);
      claimed = true;
    }
  }

  if (claimed) {
    this->decoded_++;
    this->per_decoder_[static_cast<size_t>(found.front().decoder)]++;
  } else if (raw.size() >= this->min_pulses_) {
    this->unknown_++;
  } else {
    this->noise_interval_++;
  }

  // Raw dump: every frame, or only the ones no decoder claimed (hidden decoders claim too).
  if (raw.size() >= this->min_pulses_ &&
      (this->raw_mode_ == RawMode::ALL || (this->raw_mode_ == RawMode::UNKNOWN_ONLY && !claimed)))
    this->log_raw_(raw, start_ms, has_rssi, rssi_dbm);
  this->last_frame_ms_ = start_ms;

  // Hidden decoders claim their frames but are not logged.
  this->finished_.clear();
  for (const auto &d : found) {
    if (this->visible_[static_cast<size_t>(d.decoder)])
      this->aggregator_.add(d, start_ms, has_rssi, rssi_dbm, this->finished_);
  }
  for (const auto &line : this->finished_)
    this->log_line_(line);
  return claimed;
}

void RfSniffer::log_raw_(const std::vector<int32_t> &raw, uint64_t start_ms, bool has_rssi, int16_t rssi_dbm) {
  const uint64_t gap =
      this->last_frame_ms_ != 0 && start_ms >= this->last_frame_ms_ ? start_ms - this->last_frame_ms_ : 0;
  const std::string ts = this->timestamp_(start_ms);
  if (has_rssi) {
    ESP_LOGD(TAG_RAW, "%s - raw pulses=%u gap=%" PRIu64 " rssi=%d", ts.c_str(), static_cast<unsigned>(raw.size()), gap,
             rssi_dbm);
  } else {
    ESP_LOGD(TAG_RAW, "%s - raw pulses=%u gap=%" PRIu64, ts.c_str(), static_cast<unsigned>(raw.size()), gap);
  }
  for (const auto &line : format_raw_lines(raw, RAW_VALUES_PER_LINE))
    ESP_LOGD(TAG_RAW, "  %s", line.c_str());
}

void RfSniffer::log_line_(const Line &line) {
  std::string text = this->timestamp_(line.first_ms) + " - " + decoder_name(line.decoder) + " " + line.fields;
  if (!line.quality.empty())
    text += " " + line.quality;
  if (line.has_rssi)
    text += " rssi=" + std::to_string(line.rssi_dbm);
  text += " rep=" + std::to_string(line.repeats);
  if (line.repeats > 1)
    text += " span=" + std::to_string(line.span_ms) + " max_gap=" + std::to_string(line.max_gap_ms);
  ESP_LOGD(TAG, "%s", text.c_str());
}

void RfSniffer::update() {
  const uint64_t now = this->now_ms_();
  const uint64_t interval_ms = now > this->interval_start_ms_ ? now - this->interval_start_ms_ : 0;
#ifdef USE_SENSOR
  if (this->frames_sensor_ != nullptr)
    this->frames_sensor_->publish_state(this->frames_);
  if (this->decoded_sensor_ != nullptr)
    this->decoded_sensor_->publish_state(this->decoded_);
  if (this->unknown_sensor_ != nullptr)
    this->unknown_sensor_->publish_state(this->unknown_);
  if (this->noise_per_minute_sensor_ != nullptr && interval_ms > 0)
    this->noise_per_minute_sensor_->publish_state(this->noise_interval_ * 60000.0f / interval_ms);
  float floor_dbm = 0;
  if (this->noise_floor_sensor_ != nullptr && this->rssi_.noise_floor(floor_dbm))
    this->noise_floor_sensor_->publish_state(floor_dbm);
#endif
#ifdef USE_TEXT_SENSOR
  if (this->per_protocol_sensor_ != nullptr) {
    std::string text;
    for (size_t i = 0; i < DECODER_COUNT; i++) {
      if (this->per_decoder_[i] == 0)
        continue;
      if (!text.empty())
        text += " ";
      text += std::string(decoder_name(static_cast<Decoder>(i))) + "=" + std::to_string(this->per_decoder_[i]);
    }
    this->per_protocol_sensor_->publish_state(text.empty() ? "none" : text);
  }
#endif
  this->noise_interval_ = 0;
  this->interval_start_ms_ = now;
  this->rssi_.reset_floor();
}

void RfSniffer::reset_statistics() {
  this->frames_ = 0;
  this->decoded_ = 0;
  this->unknown_ = 0;
  for (auto &count : this->per_decoder_)
    count = 0;
  // update() publishes the zeroed values and starts a new noise interval.
  this->update();
  ESP_LOGI(TAG, "Statistics reset");
}

void RfSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "RF sniffer:");
  std::string available;
  for (size_t i = 0; i < DECODER_COUNT; i++) {
    if (!decoder_available(static_cast<Decoder>(i)))
      continue;
    if (!available.empty())
      available += ", ";
    available += decoder_name(static_cast<Decoder>(i));
  }
  ESP_LOGCONFIG(TAG, "  Decoders: %s", available.c_str());
  const auto &t = this->oregon_.get_timing();
  ESP_LOGCONFIG(TAG,
                "  Oregon: pulse window %" PRIu32 "-%" PRIu32 " us, long pulse from %" PRIu32 " us, end gap %" PRIu32
                " us, preamble %u, verify bit pairs %s",
                t.min_pulse_us, t.max_pulse_us, t.long_pulse_us, t.end_gap_us, t.min_preamble,
                YESNO(this->oregon_.get_verify_bit_pairs()));
  ESP_LOGCONFIG(TAG, "  Max repeat wait: %" PRIu32 " ms, min pulses %" PRIu32, this->max_repeat_wait_ms_,
                this->min_pulses_);
#ifdef USE_RF_SNIFFER_RSSI
  ESP_LOGCONFIG(TAG, "  RSSI: %s", this->radio_ != nullptr ? "CC1101, sampled every 1 ms" : "no");
#else
  ESP_LOGCONFIG(TAG, "  RSSI: no");
#endif
#ifdef USE_RF_SNIFFER_TIME
  ESP_LOGCONFIG(TAG, "  Time source: %s", this->time_ != nullptr ? "yes" : "no");
#else
  ESP_LOGCONFIG(TAG, "  Time source: no");
#endif
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace rf_sniffer
}  // namespace esphome
