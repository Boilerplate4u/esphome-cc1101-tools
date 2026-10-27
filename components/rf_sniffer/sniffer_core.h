#pragma once

// ESPHome independent parts of rf_sniffer, covered by the host tests in tests/.
//
// - RepeatAggregator collapses identical transmissions into one line with a
//   repeat counter.
// - OregonSniffer decodes Oregon Scientific v2.1 messages (known and unknown
//   types) into log lines.
// - RssiTracker keeps the few RSSI samples taken during frames for the peak of
//   each frame, and a histogram of quiet time samples for the noise floor.
// - format helpers for timestamps and raw pulse dumps.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "manchester_decoder.h"
#include "oregon_protocol.h"

namespace esphome {
namespace rf_sniffer {

/// Decoders in the order they are tried. Oregon first (Manchester), the specific
/// built-in protocols next and the generic rc_switch last.
enum class Decoder : uint8_t {
  OREGON,
  NEXA,
  KEELOQ,
  DOOYA,
  DRAYTON,
  BYRONSX,
  BRENNENSTUHL,
  NEXUS,        // ESPHome dev only
  AOK,          // ESPHome dev only
  CARDIN_S438,  // ESPHome dev only
  RC_SWITCH,
  COUNT,
};
static constexpr size_t DECODER_COUNT = static_cast<size_t>(Decoder::COUNT);

const char *decoder_name(Decoder decoder);

enum class RawMode : uint8_t { OFF, UNKNOWN_ONLY, ALL };

/// One decoded transmission, before repeats are counted.
struct Decoded {
  Decoder decoder{Decoder::OREGON};
  /// Identifies the sender (for example protocol, channel and unit). A new value
  /// for a pending sender ends that sender's line at once.
  std::string sender;
  /// The decoded fields as "key=value ...", also the key for repeats.
  std::string fields;
  /// Extra quality fields that may differ between copies ("short_us=487 ...").
  /// Taken from the first copy.
  std::string quality;
  /// An Oregon message that was found but failed its checks (fields hold the reason).
  bool rejected{false};
};

/// A finished line: one transmission with all its copies.
struct Line {
  Decoder decoder{Decoder::OREGON};
  std::string fields;
  std::string quality;
  uint64_t first_ms{0};    // start of the first copy
  uint32_t span_ms{0};     // start of the first to start of the last copy
  uint32_t max_gap_ms{0};  // largest start to start time between two copies
  uint16_t repeats{0};
  bool has_rssi{false};
  int16_t rssi_dbm{0};  // strongest copy
};

class RepeatAggregator {
 public:
  static constexpr size_t MAX_PENDING = 8;

  /// Adds a copy that started at `start_ms` and was received completely at `end_ms`.
  /// Lines that end because of it (same sender, other data, or the pending table is
  /// full) are appended to `out`.
  void add(const Decoded &decoded, uint64_t start_ms, uint64_t end_ms, bool has_rssi, int16_t rssi_dbm,
           std::vector<Line> &out);
  /// Ends every line whose last copy was received at least `max_wait_ms` ago.
  void poll(uint64_t now_ms, uint32_t max_wait_ms, std::vector<Line> &out);
  /// Ends every pending line.
  void flush(std::vector<Line> &out);
  size_t pending() const;

 protected:
  struct Entry {
    bool used{false};
    std::string sender;
    Line line;
    uint64_t last_start_ms{0};
    uint64_t last_end_ms{0};
  };
  void finish_(Entry &entry, std::vector<Line> &out);
  Entry entries_[MAX_PENDING];
};

/// Decodes every Oregon v2.1 message in a capture.
class OregonSniffer {
 public:
  void set_timing(const ManchesterDecoder::Timing &timing) { this->decoder_.set_timing(timing); }
  const ManchesterDecoder::Timing &get_timing() const { return this->decoder_.get_timing(); }
  void set_verify_bit_pairs(bool verify) { this->verify_bit_pairs_ = verify; }
  bool get_verify_bit_pairs() const { return this->verify_bit_pairs_; }

  /// Pulse widths in microseconds, signs as delivered by remote_receiver.
  /// Appends one Decoded per message found, accepted or rejected, and returns
  /// how many were found.
  size_t decode(const std::vector<int32_t> &raw, std::vector<Decoded> &out);

  /// Messages rejected since boot (bit pair errors, checksum, values). Each one is
  /// also returned by decode() with `rejected` set.
  uint32_t rejected() const { return this->rejected_; }

 protected:
  bool handle_message_(std::vector<Decoded> &out);
  ManchesterDecoder decoder_;
  bool verify_bit_pairs_{true};
  uint32_t rejected_{0};
};

/// RSSI samples taken during frames, and a noise floor histogram.
class RssiTracker {
 public:
  static constexpr size_t SAMPLES = 32;

  /// A sample taken during a frame. `raw` is the CC1101 RSSI register value
  /// (two's complement, 0.5 dB steps).
  void add(uint64_t time_ms, uint8_t raw);
  /// A sample taken while no frame was being received.
  void add_floor(uint8_t raw);
  /// Strongest frame sample between `from_ms` and `to_ms` (inclusive).
  bool peak(uint64_t from_ms, uint64_t to_ms, int16_t &dbm) const;
  /// Median of the floor samples since the last reset_floor(), in dBm.
  bool noise_floor(float &dbm) const;
  void reset_floor();

  static float raw_to_dbm(uint8_t raw);

 protected:
  // Times are the low 32 bits of the uptime.
  uint32_t times_[SAMPLES]{};
  uint8_t raws_[SAMPLES]{};
  size_t next_{0};
  size_t count_{0};
  uint32_t histogram_[256]{};
  uint32_t histogram_total_{0};
};

/// Signal grade 1..5 from the margin over the noise floor: 25, 18, 12 and 6 dB.
int signal_grade(float snr_db);
/// "signal_grade=2 (snr_db=7, rssi_dbm=-76)", "rssi_dbm=-76" without a noise
/// floor, or empty without RSSI.
std::string format_signal(bool has_rssi, int16_t rssi_dbm, bool has_floor, float floor_dbm);
/// A decoded line without the timestamp: "nexa device=... signal_grade=... rep=5 span_ms=..."
std::string format_line(const Line &line, bool has_floor, float floor_dbm);
/// True when a capture filled the receiver buffer of `receive_symbols` RMT symbols
/// (0: unknown, never truncated).
bool capture_truncated(size_t pulses, uint32_t receive_symbols);
/// "unknown pulses=897 duration_ms=910 truncated=yes signal_grade=1 (...)"
std::string format_unknown(size_t pulses, uint64_t duration_ms, bool truncated, const std::string &signal);

/// "HH:MM:SS.mmm" from local wall clock parts, or "up S.mmm" from the uptime.
std::string format_wall_time(int hour, int minute, int second, int millisecond);
std::string format_uptime(uint64_t uptime_ms);

/// Raw pulses, `per_line` values per line, as "1024, -981, ...".
std::vector<std::string> format_raw_lines(const std::vector<int32_t> &raw, size_t per_line);

/// Sum of all pulse widths in microseconds.
uint64_t capture_duration_us(const std::vector<int32_t> &raw);

}  // namespace rf_sniffer
}  // namespace esphome
