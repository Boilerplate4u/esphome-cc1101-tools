#include "sniffer_core.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace esphome {
namespace rf_sniffer {

const char *decoder_name(Decoder decoder) {
  switch (decoder) {
    case Decoder::OREGON:
      return "oregon";
    case Decoder::NEXA:
      return "nexa";
    case Decoder::KEELOQ:
      return "keeloq";
    case Decoder::DOOYA:
      return "dooya";
    case Decoder::DRAYTON:
      return "drayton";
    case Decoder::BYRONSX:
      return "byronsx";
    case Decoder::BRENNENSTUHL:
      return "brennenstuhl";
    case Decoder::NEXUS:
      return "nexus";
    case Decoder::AOK:
      return "aok";
    case Decoder::CARDIN_S438:
      return "cardin_s438";
    case Decoder::RC_SWITCH:
      return "rc_switch";
    case Decoder::COUNT:
      break;
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// RepeatAggregator

void RepeatAggregator::finish_(Entry &entry, std::vector<Line> &out) {
  out.push_back(entry.line);
  entry = Entry{};
}

void RepeatAggregator::add(const Decoded &decoded, uint64_t start_ms, uint64_t end_ms, bool has_rssi, int16_t rssi_dbm,
                           std::vector<Line> &out) {
  Entry *same_sender = nullptr;
  for (auto &entry : this->entries_) {
    if (entry.used && entry.sender == decoded.sender && entry.line.decoder == decoded.decoder) {
      same_sender = &entry;
      break;
    }
  }
  if (same_sender != nullptr) {
    if (same_sender->line.fields == decoded.fields) {
      Line &line = same_sender->line;
      const uint64_t gap = start_ms >= same_sender->last_start_ms ? start_ms - same_sender->last_start_ms : 0;
      if (gap > line.max_gap_ms)
        line.max_gap_ms = static_cast<uint32_t>(gap);
      line.span_ms = static_cast<uint32_t>(start_ms >= line.first_ms ? start_ms - line.first_ms : 0);
      if (line.repeats < UINT16_MAX)
        line.repeats++;
      if (has_rssi && (!line.has_rssi || rssi_dbm > line.rssi_dbm)) {
        line.has_rssi = true;
        line.rssi_dbm = rssi_dbm;
      }
      same_sender->last_start_ms = start_ms;
      same_sender->last_end_ms = end_ms;
      return;
    }
    // Same sender with new data: its previous transmission is complete.
    this->finish_(*same_sender, out);
  }

  Entry *slot = nullptr;
  for (auto &entry : this->entries_) {
    if (!entry.used) {
      slot = &entry;
      break;
    }
  }
  if (slot == nullptr) {
    // Table full: end the line that has waited longest.
    slot = &this->entries_[0];
    for (auto &entry : this->entries_) {
      if (entry.last_end_ms < slot->last_end_ms)
        slot = &entry;
    }
    this->finish_(*slot, out);
  }
  slot->used = true;
  slot->sender = decoded.sender;
  slot->last_start_ms = start_ms;
  slot->last_end_ms = end_ms;
  slot->line = Line{};
  slot->line.decoder = decoded.decoder;
  slot->line.fields = decoded.fields;
  slot->line.quality = decoded.quality;
  slot->line.first_ms = start_ms;
  slot->line.repeats = 1;
  slot->line.has_rssi = has_rssi;
  slot->line.rssi_dbm = rssi_dbm;
}

void RepeatAggregator::poll(uint64_t now_ms, uint32_t max_wait_ms, std::vector<Line> &out) {
  // Oldest first, so the log stays in reception order.
  while (true) {
    Entry *oldest = nullptr;
    for (auto &entry : this->entries_) {
      if (entry.used && now_ms >= entry.last_end_ms && now_ms - entry.last_end_ms >= max_wait_ms &&
          (oldest == nullptr || entry.line.first_ms < oldest->line.first_ms))
        oldest = &entry;
    }
    if (oldest == nullptr)
      return;
    this->finish_(*oldest, out);
  }
}

void RepeatAggregator::flush(std::vector<Line> &out) { this->poll(UINT64_MAX, 0, out); }

size_t RepeatAggregator::pending() const {
  size_t n = 0;
  for (const auto &entry : this->entries_)
    n += entry.used ? 1 : 0;
  return n;
}

// ---------------------------------------------------------------------------
// OregonSniffer

static std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static std::string format(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n < 0)
    return std::string();
  return std::string(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
}

size_t OregonSniffer::decode(const std::vector<int32_t> &raw, std::vector<Decoded> &out) {
  const size_t before = out.size();
  this->decoder_.reset();
  for (int32_t value : raw) {
    // Clamp before negating to avoid INT32_MIN overflow.
    const uint32_t width = value < 0 ? uint32_t(-(int64_t) value) : uint32_t(value);
    if (this->decoder_.feed(width)) {
      this->handle_message_(out);
      this->decoder_.reset();
    }
  }
  if (this->decoder_.finish())
    this->handle_message_(out);
  this->decoder_.reset();
  return out.size() - before;
}

// Reason keys for the log, one word each so the line stays key=value.
static const char *reason_key(ParseResult result) {
  switch (result) {
    case ParseResult::TOO_SHORT:
      return "too_short";
    case ParseResult::BAD_SYNC:
      return "bad_sync";
    case ParseResult::UNKNOWN_TYPE:
      return "unknown_type";
    case ParseResult::BAD_CHECKSUM:
      return "bad_checksum";
    case ParseResult::BAD_VALUE:
      return "bad_value";
    case ParseResult::OK:
      break;
  }
  return "ok";
}

// A found message that failed its checks. The nibbles identify identical copies.
static Decoded rejected_message(const uint8_t *data, size_t len, const char *reason, uint16_t pair_errors,
                                const std::string &quality) {
  std::string nibbles;
  for (size_t i = 0; i < len * 2; i++)
    nibbles += "0123456789ABCDEF"[oregon_nibble(data, i)];
  Decoded d;
  d.decoder = Decoder::OREGON;
  d.rejected = true;
  d.sender = "rejected:" + nibbles;
  d.fields = format("rejected reason=%s nibbles=%s", reason, nibbles.c_str());
  d.quality = quality + format(" pair_errors=%u", pair_errors);
  return d;
}

bool OregonSniffer::handle_message_(std::vector<Decoded> &out) {
  const uint8_t *data = this->decoder_.data();
  const size_t len = this->decoder_.size();
  const auto &stats = this->decoder_.pulse_stats();
  const std::string quality =
      format("short_us=%" PRIu32 " long_us=%" PRIu32, stats.short_avg_us(), stats.long_avg_us());

  OregonReading reading;
  const ParseResult result = parse_oregon_v21(data, len, reading);
  // Cut off before the end of its checksum: not a message (as oregon_v21_receiver).
  if (this->decoder_.truncated() && result == ParseResult::TOO_SHORT)
    return false;

  if (result == ParseResult::UNKNOWN_TYPE) {
    UnknownOregonMessage msg;
    if (!parse_unknown_oregon_v21(data, len, msg)) {
      this->rejected_++;
      out.push_back(rejected_message(data, len, "no_checksum", this->decoder_.pair_errors(len), quality));
      return false;
    }
    const uint16_t pair_errors = this->decoder_.pair_errors(msg.used_bytes);
    if (pair_errors > 0 && this->verify_bit_pairs_) {
      this->rejected_++;
      out.push_back(rejected_message(data, msg.used_bytes, "pair_errors", pair_errors, quality));
      return false;
    }
    std::string nibbles;
    for (size_t i = 0; i <= size_t(msg.last_checksummed_nibble) + 2; i++)
      nibbles += "0123456789ABCDEF"[oregon_nibble(data, i)];
    Decoded d;
    d.decoder = Decoder::OREGON;
    d.sender = format("unknown:%04X:%u:%02X", msg.type, msg.channel_raw, msg.unit_id);
    d.fields = format("unknown type=%04X channel=%u unit_id=%02X flags=%X checksum_nibbles=%u nibbles=%s", msg.type,
                      msg.channel_raw, msg.unit_id, msg.flags, msg.last_checksummed_nibble, nibbles.c_str());
    if (msg.checksum_matches > 1)
      d.fields += " checksum_positions=" + std::to_string(msg.checksum_matches);
    if (msg.has_guessed_temperature)
      d.fields += format(" guessed_temp=%.1f", msg.guessed_temperature);
    if (msg.has_guessed_humidity)
      d.fields += format(" guessed_hum=%u", msg.guessed_humidity);
    d.quality = quality + format(" pair_errors=%u", pair_errors);
    out.push_back(d);
    return true;
  }

  const size_t checked_bytes = reading.used_bytes != 0 ? reading.used_bytes : len;
  const uint16_t pair_errors = this->decoder_.pair_errors(checked_bytes);
  if (pair_errors > 0 && this->verify_bit_pairs_) {
    this->rejected_++;
    out.push_back(rejected_message(data, checked_bytes, "pair_errors", pair_errors, quality));
    return false;
  }
  if (result != ParseResult::OK) {
    this->rejected_++;
    out.push_back(rejected_message(data, checked_bytes, reason_key(result), pair_errors, quality));
    return false;
  }

  Decoded d;
  d.decoder = Decoder::OREGON;
  d.sender = format("%04X:%u:%02X", reading.type, reading.channel, reading.unit_id);
  d.fields = format("%s type=%04X channel=%u unit_id=%02X temp=%.1f", reading.model, reading.type, reading.channel,
                    reading.unit_id, reading.temperature);
  if (reading.has_humidity)
    d.fields += format(" hum=%u", reading.humidity);
  d.fields += reading.battery_low ? " battery=low" : " battery=ok";
  d.quality = quality + format(" pair_errors=%u", pair_errors);
  out.push_back(d);
  return true;
}

// ---------------------------------------------------------------------------
// RssiTracker

float RssiTracker::raw_to_dbm(uint8_t raw) {
  // CC1101 datasheet: RSSI is a two's complement value in 0.5 dB steps; the
  // offset matches the cc1101 component (RSSI_OFFSET 74 dB).
  return static_cast<int8_t>(raw) * 0.5f - 74.0f;
}

void RssiTracker::add(uint64_t time_ms, uint8_t raw) {
  this->times_[this->next_] = static_cast<uint32_t>(time_ms);
  this->raws_[this->next_] = raw;
  this->next_ = (this->next_ + 1) % SAMPLES;
  if (this->count_ < SAMPLES)
    this->count_++;
}

void RssiTracker::add_floor(uint8_t raw) {
  this->histogram_[static_cast<uint8_t>(static_cast<int8_t>(raw) + 128)]++;
  this->histogram_total_++;
}

bool RssiTracker::peak(uint64_t from_ms, uint64_t to_ms, int16_t &dbm) const {
  bool found = false;
  int best = -128;
  if (to_ms < from_ms)
    return false;
  // Wrap safe: compare the offset from `from_ms` in 32 bits.
  const uint32_t from = static_cast<uint32_t>(from_ms);
  const uint32_t width = static_cast<uint32_t>(to_ms - from_ms);
  for (size_t i = 0; i < this->count_; i++) {
    if (static_cast<uint32_t>(this->times_[i] - from) > width)
      continue;
    const int v = static_cast<int8_t>(this->raws_[i]);
    if (!found || v > best) {
      best = v;
      found = true;
    }
  }
  if (found) {
    const float f = raw_to_dbm(static_cast<uint8_t>(static_cast<int8_t>(best)));
    dbm = static_cast<int16_t>(f >= 0 ? f + 0.5f : f - 0.5f);
  }
  return found;
}

bool RssiTracker::noise_floor(float &dbm) const {
  if (this->histogram_total_ == 0)
    return false;
  const uint32_t half = (this->histogram_total_ + 1) / 2;
  uint32_t seen = 0;
  for (int i = 0; i < 256; i++) {
    seen += this->histogram_[i];
    if (seen >= half) {
      dbm = raw_to_dbm(static_cast<uint8_t>(static_cast<int8_t>(i - 128)));
      return true;
    }
  }
  return false;
}

void RssiTracker::reset_floor() {
  for (auto &h : this->histogram_)
    h = 0;
  this->histogram_total_ = 0;
}

// ---------------------------------------------------------------------------
// Signal and line formatting

int signal_grade(float snr_db) {
  if (snr_db >= 25.0f)
    return 5;
  if (snr_db >= 18.0f)
    return 4;
  if (snr_db >= 12.0f)
    return 3;
  if (snr_db >= 6.0f)
    return 2;
  return 1;
}

std::string format_signal(bool has_rssi, int16_t rssi_dbm, bool has_floor, float floor_dbm) {
  if (!has_rssi)
    return std::string();
  if (!has_floor)
    return format("rssi_dbm=%d", rssi_dbm);
  const float snr = static_cast<float>(rssi_dbm) - floor_dbm;
  const int snr_db = static_cast<int>(snr >= 0 ? snr + 0.5f : snr - 0.5f);
  return format("signal_grade=%d (snr_db=%d, rssi_dbm=%d)", signal_grade(snr), snr_db, rssi_dbm);
}

std::string format_line(const Line &line, bool has_floor, float floor_dbm) {
  std::string text = std::string(decoder_name(line.decoder)) + " " + line.fields;
  if (!line.quality.empty())
    text += " " + line.quality;
  const std::string signal = format_signal(line.has_rssi, line.rssi_dbm, has_floor, floor_dbm);
  if (!signal.empty())
    text += " " + signal;
  text += " rep=" + std::to_string(line.repeats);
  if (line.repeats > 1)
    text += " span_ms=" + std::to_string(line.span_ms) + " max_gap_ms=" + std::to_string(line.max_gap_ms);
  return text;
}

bool capture_truncated(size_t pulses, uint32_t receive_symbols) {
  // Every RMT symbol holds two pulses; a full buffer gives about 2 * symbols
  // pulses (a few less when the receiver filter merged short ones).
  return receive_symbols != 0 && pulses + 4 >= size_t(receive_symbols) * 2;
}

std::string format_unknown(size_t pulses, uint64_t duration_ms, bool truncated, const std::string &signal) {
  std::string text = format("unknown pulses=%u duration_ms=%" PRIu64 " truncated=%s", static_cast<unsigned>(pulses),
                            duration_ms, truncated ? "yes" : "no");
  if (!signal.empty())
    text += " " + signal;
  return text;
}

// ---------------------------------------------------------------------------
// Formatting

std::string format_wall_time(int hour, int minute, int second, int millisecond) {
  return format("%02d:%02d:%02d.%03d", hour, minute, second, millisecond);
}

std::string format_uptime(uint64_t uptime_ms) {
  return format("up %" PRIu64 ".%03u", uptime_ms / 1000, static_cast<unsigned>(uptime_ms % 1000));
}

std::vector<std::string> format_raw_lines(const std::vector<int32_t> &raw, size_t per_line) {
  std::vector<std::string> lines;
  std::string line;
  for (size_t i = 0; i < raw.size(); i++) {
    if (!line.empty())
      line += ", ";
    line += std::to_string(raw[i]);
    if ((i + 1) % per_line == 0 || i + 1 == raw.size()) {
      lines.push_back(line);
      line.clear();
    }
  }
  return lines;
}

uint64_t capture_duration_us(const std::vector<int32_t> &raw) {
  uint64_t total = 0;
  for (int32_t v : raw)
    total += v < 0 ? uint64_t(-(int64_t) v) : uint64_t(v);
  return total;
}

FrameTiming frame_timing(const std::vector<int32_t> &raw, uint64_t received_ms) {
  FrameTiming t;
  const uint64_t total_ms = capture_duration_us(raw) / 1000;
  t.start_ms = received_ms > total_ms ? received_ms - total_ms : 0;
  uint64_t pulses_us = capture_duration_us(raw);
  if (!raw.empty()) {
    const int32_t idle = raw.back();
    pulses_us -= idle < 0 ? uint64_t(-(int64_t) idle) : uint64_t(idle);
  }
  t.duration_ms = pulses_us / 1000;
  t.rssi_from_ms = t.start_ms > RSSI_WINDOW_MARGIN_MS ? t.start_ms - RSSI_WINDOW_MARGIN_MS : 0;
  return t;
}

}  // namespace rf_sniffer
}  // namespace esphome
