// Host tests for the ESPHome independent parts of rf_sniffer.
// Build and run with tests/run_tests.sh (Debian/WSL, Linux or macOS).

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sniffer_core.h"

using namespace esphome::rf_sniffer;

static int failures = 0;
#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

struct Capture {
  std::string label;
  std::vector<int32_t> raw;
};

static std::vector<Capture> load(const std::string &path) {
  std::vector<Capture> out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const size_t sep = line.find(';');
    if (sep == std::string::npos)
      continue;
    Capture c;
    c.label = line.substr(0, sep);
    std::stringstream ss(line.substr(sep + 1));
    std::string v;
    while (std::getline(ss, v, ','))
      c.raw.push_back(std::stoi(v));
    out.push_back(c);
  }
  return out;
}

static const Capture *find(const std::vector<Capture> &caps, const std::string &prefix) {
  for (const auto &c : caps)
    if (c.label.rfind(prefix, 0) == 0)
      return &c;
  return nullptr;
}

static Decoded decoded(const char *sender, const char *fields) {
  Decoded d;
  d.decoder = Decoder::NEXA;
  d.sender = sender;
  d.fields = fields;
  return d;
}

static void test_oregon_real_captures(const std::vector<Capture> &caps) {
  OregonSniffer oregon;
  struct Expect {
    const char *label;
    const char *fields;
  } expects[] = {
      {"oregon ch1 2026", "THGR122N/THGR228N type=1D20 channel=1 unit_id=7B temp=10.7 hum=98 battery=ok"},
      {"oregon ch3", "THGR122N/THGR228N type=1D20 channel=3 unit_id=F5 temp=12.2 hum=72 battery=ok"},
      {"oregon ch2 skewed", "THGR122N/THGR228N type=1D20 channel=2 unit_id=BE temp=8.3 hum=61 battery=ok"},
      {"oregon ch1 short preamble (23)", "THGR122N/THGR228N type=1D20 channel=1 unit_id=7B temp=8.9 hum=97 battery=ok"},
      {"oregon ch1 short preamble (21)", "THGR122N/THGR228N type=1D20 channel=1 unit_id=7B temp=9.0 hum=97 battery=ok"},
  };
  for (const auto &e : expects) {
    const Capture *c = find(caps, e.label);
    CHECK(c != nullptr);
    if (c == nullptr)
      continue;
    std::vector<Decoded> out;
    CHECK(oregon.decode(c->raw, out) == 1);
    if (out.size() == 1) {
      if (out[0].fields != e.fields)
        std::printf("  %s: got '%s'\n", e.label, out[0].fields.c_str());
      CHECK(out[0].fields == e.fields);
      CHECK(out[0].quality.find("pair_errors=0") != std::string::npos);
    }
  }
  // Not Oregon: Nexa, the unknown device and noise give nothing.
  for (const char *label : {"nexa on", "unknown device", "noise"}) {
    std::vector<Decoded> out;
    const Capture *c = find(caps, label);
    CHECK(c != nullptr && oregon.decode(c->raw, out) == 0);
  }
  // The short preambles need min_preamble 20; with the old default 24 they are lost.
  OregonSniffer strict;
  ManchesterDecoder::Timing t;
  t.min_preamble = 24;
  strict.set_timing(t);
  std::vector<Decoded> out;
  CHECK(strict.decode(find(caps, "oregon ch1 short preamble (21)")->raw, out) == 0);
}

// Same encoder as tests/test_helpers.h in oregon_v21_receiver: v2.1 sends every
// data bit followed by its complement, after a preamble of long pulses.
static std::vector<int32_t> encode_v21(const std::vector<uint8_t> &msg) {
  std::vector<uint8_t> bits;
  for (uint8_t byte : msg) {
    for (int bit = 0; bit < 8; bit++) {
      const uint8_t b = (byte >> bit) & 1;
      bits.push_back(b);
      bits.push_back(b ^ 1);
    }
  }
  std::vector<int32_t> pulses;
  int sign = 1;
  auto emit = [&](int32_t w) {
    pulses.push_back(sign * w);
    sign = -sign;
  };
  for (int i = 0; i < 32; i++)
    emit(976);
  emit(488);
  emit(488);
  for (size_t i = 1; i < bits.size(); i++) {
    if (bits[i] == bits[i - 1]) {
      emit(488);
      emit(488);
    } else {
      emit(976);
    }
  }
  pulses.push_back(-6000);
  return pulses;
}

static void set_nibble(std::vector<uint8_t> &m, size_t n, uint8_t v) {
  uint8_t &b = m[n / 2];
  b = (n & 1) ? uint8_t((b & 0x0F) | (v << 4)) : uint8_t((b & 0xF0) | (v & 0x0F));
}

static void test_oregon_unknown_type() {
  // The channel 1 message from 2026-10-25 with its type changed to 1A3D.
  std::vector<uint8_t> m = {0x1A, 0x2D, 0x10, 0x7B, 0x10, 0x09, 0x80, 0xC9, 0x4A, 0xCC};
  set_nibble(m, 2, 0xA);
  set_nibble(m, 3, 0x3);
  set_nibble(m, 4, 0xD);
  uint8_t sum = 0;
  for (size_t i = 1; i <= 15; i++)
    sum += oregon_nibble(m.data(), i);
  set_nibble(m, 16, sum & 0x0F);
  set_nibble(m, 17, sum >> 4);
  OregonSniffer oregon;
  std::vector<Decoded> out;
  const size_t n = oregon.decode(encode_v21(m), out);
  CHECK(n == 1);
  if (n == 1) {
    CHECK(out[0].fields == "unknown type=1A3D channel=1 unit_id=7B flags=0 checksum_nibbles=15 "
                           "nibbles=A1A3D1B70190089C55 guessed_temp=9.1 guessed_hum=98");
    CHECK(out[0].sender == "unknown:1A3D:1:7B");
  }
}

static void test_oregon_rejected() {
  // The channel 1 message from 2026-10-25 with a wrong checksum.
  std::vector<uint8_t> m = {0x1A, 0x2D, 0x10, 0x7B, 0x10, 0x09, 0x80, 0xC9, 0x4A, 0xCC};
  set_nibble(m, 16, (oregon_nibble(m.data(), 16) + 1) & 0x0F);
  OregonSniffer oregon;
  std::vector<Decoded> out;
  CHECK(oregon.decode(encode_v21(m), out) == 1);
  CHECK(oregon.rejected() == 1);
  if (out.size() == 1) {
    CHECK(out[0].rejected);
    CHECK(out[0].fields.rfind("rejected reason=bad_checksum nibbles=A1D201B7", 0) == 0);
    CHECK(out[0].sender.rfind("rejected:", 0) == 0);
    CHECK(out[0].quality.find("pair_errors=0") != std::string::npos);
  }
  // A valid message is not marked rejected.
  out.clear();
  std::vector<uint8_t> ok = {0x1A, 0x2D, 0x10, 0x7B, 0x10, 0x09, 0x80, 0xC9, 0x4A, 0xCC};
  CHECK(oregon.decode(encode_v21(ok), out) == 1 && !out[0].rejected);
}

static void test_repeats() {
  RepeatAggregator agg;
  std::vector<Line> out;
  // Nexa: 5 copies 90 ms apart, then off.
  for (int i = 0; i < 5; i++)
    agg.add(decoded("13D2A9A:3", "device=13D2A9A state=on"), 1000 + i * 90, 1000 + i * 90 + 76, true, -60 + i, out);
  CHECK(out.empty());
  agg.poll(1000 + 4 * 90 + 499, 500, out);
  CHECK(out.empty());  // still waiting
  agg.add(decoded("13D2A9A:3", "device=13D2A9A state=off"), 1000 + 4 * 90 + 600, 1000 + 4 * 90 + 676, true, -70, out);
  CHECK(out.size() == 1);  // new data from the same sender ends the line at once
  if (!out.empty()) {
    CHECK(out[0].repeats == 5);
    CHECK(out[0].first_ms == 1000);
    CHECK(out[0].span_ms == 360);
    CHECK(out[0].max_gap_ms == 90);
    CHECK(out[0].has_rssi && out[0].rssi_dbm == -56);
    CHECK(out[0].fields == "device=13D2A9A state=on");
  }
  out.clear();
  agg.poll(1000 + 4 * 90 + 676 + 499, 500, out);
  CHECK(out.empty());  // the wait counts from the end of the last copy
  agg.poll(1000 + 4 * 90 + 676 + 500, 500, out);
  CHECK(out.size() == 1 && out[0].repeats == 1 && out[0].fields == "device=13D2A9A state=off");

  // Two senders interleaved stay separate; output is in reception order.
  out.clear();
  agg.add(decoded("a", "x=1"), 5000, 5010, false, 0, out);
  agg.add(decoded("b", "y=1"), 5100, 5110, false, 0, out);
  agg.add(decoded("a", "x=1"), 5240, 5250, false, 0, out);
  agg.poll(6000, 500, out);
  CHECK(out.size() == 2);
  if (out.size() == 2) {
    CHECK(out[0].fields == "x=1" && out[0].repeats == 2 && !out[0].has_rssi);
    CHECK(out[1].fields == "y=1" && out[1].repeats == 1);
  }
  // A full table ends the oldest line.
  out.clear();
  for (int i = 0; i < 9; i++)
    agg.add(decoded(std::to_string(i).c_str(), "z=1"), 7000 + i, 7010 + i, false, 0, out);
  CHECK(out.size() == 1 && out[0].first_ms == 7000);
  CHECK(agg.pending() == RepeatAggregator::MAX_PENDING);
  out.clear();
  agg.flush(out);
  CHECK(out.size() == RepeatAggregator::MAX_PENDING && agg.pending() == 0);

  // Oregon copies of 192 ms whose second copy starts 446 ms after the first: the
  // second copy is complete at 638 ms, after the 500 ms wait counted from the start.
  out.clear();
  agg.add(decoded("1D20:1:7B", "temp=11.3"), 10000, 10192, false, 0, out);
  agg.poll(10500, 500, out);
  CHECK(out.empty());
  agg.add(decoded("1D20:1:7B", "temp=11.3"), 10446, 10638, false, 0, out);
  agg.poll(11137, 500, out);
  CHECK(out.empty());
  agg.poll(11138, 500, out);
  CHECK(out.size() == 1 && out[0].repeats == 2 && out[0].span_ms == 446 && out[0].max_gap_ms == 446);
}

static void test_rssi() {
  CHECK(std::fabs(RssiTracker::raw_to_dbm(0x00) + 74.0f) < 0.01f);
  CHECK(std::fabs(RssiTracker::raw_to_dbm(0x14) + 64.0f) < 0.01f);   // +20 -> -64 dBm
  CHECK(std::fabs(RssiTracker::raw_to_dbm(0xC4) + 104.0f) < 0.01f);  // -60 -> -104 dBm
  RssiTracker rssi;
  rssi.add(410, static_cast<uint8_t>(int8_t(20)));  // frame reads: -64, -59, -62 dBm
  rssi.add(420, static_cast<uint8_t>(int8_t(30)));
  rssi.add(430, static_cast<uint8_t>(int8_t(24)));
  rssi.add(900, static_cast<uint8_t>(int8_t(-10)));  // another frame, -79 dBm
  int16_t peak = 0;
  CHECK(rssi.peak(400, 599, peak) && peak == -59);
  CHECK(rssi.peak(800, 1000, peak) && peak == -79);
  CHECK(!rssi.peak(5000, 6000, peak));
  float floor_dbm = 0;
  CHECK(!rssi.noise_floor(floor_dbm));  // frame reads do not count for the floor
  for (int i = 0; i < 9; i++)
    rssi.add_floor(static_cast<uint8_t>(int8_t(i < 6 ? -60 : -20)));  // mostly -104 dBm
  CHECK(rssi.noise_floor(floor_dbm) && std::fabs(floor_dbm + 104.0f) < 0.01f);
  // The ring keeps the newest SAMPLES reads, also across the 32 bit time wrap.
  for (uint64_t i = 0; i < RssiTracker::SAMPLES; i++)
    rssi.add(0xFFFFFFF0ull + i, static_cast<uint8_t>(int8_t(i == 20 ? 40 : 0)));  // -54 dBm at the peak
  CHECK(rssi.peak(0xFFFFFFF0ull, 0xFFFFFFF0ull + RssiTracker::SAMPLES, peak) && peak == -54);
  CHECK(!rssi.peak(400, 599, peak));
  rssi.reset_floor();
  CHECK(!rssi.noise_floor(floor_dbm));
}

static void test_signal_and_lines() {
  CHECK(signal_grade(30) == 5 && signal_grade(25) == 5 && signal_grade(24.9f) == 4 && signal_grade(18) == 4);
  CHECK(signal_grade(17) == 3 && signal_grade(12) == 3 && signal_grade(11) == 2 && signal_grade(6) == 2);
  CHECK(signal_grade(5.9f) == 1 && signal_grade(-3) == 1);
  CHECK(format_signal(true, -76, true, -83.0f) == "signal_grade=2 (snr_db=7, rssi_dbm=-76)");
  CHECK(format_signal(true, -76, true, -83.5f) == "signal_grade=2 (snr_db=8, rssi_dbm=-76)");
  CHECK(format_signal(true, -80, false, 0) == "rssi_dbm=-80");
  CHECK(format_signal(false, 0, true, -83.0f).empty());

  Line line;
  line.decoder = Decoder::OREGON;
  line.fields = "THGR122N/THGR228N type=1D20 channel=2";
  line.quality = "short_us=488 long_us=975 pair_errors=0";
  line.repeats = 2;
  line.span_ms = 241;
  line.max_gap_ms = 241;
  line.has_rssi = true;
  line.rssi_dbm = -76;
  CHECK(format_line(line, true, -83.0f) ==
        "oregon THGR122N/THGR228N type=1D20 channel=2 short_us=488 long_us=975 pair_errors=0 "
        "signal_grade=2 (snr_db=7, rssi_dbm=-76) rep=2 span_ms=241 max_gap_ms=241");
  line.repeats = 1;
  line.has_rssi = false;
  line.quality.clear();
  CHECK(format_line(line, true, -83.0f) == "oregon THGR122N/THGR228N type=1D20 channel=2 rep=1");

  CHECK(capture_truncated(897, 448) && capture_truncated(892, 448) && !capture_truncated(891, 448));
  CHECK(!capture_truncated(5000, 0));
  CHECK(format_unknown(897, 910, true, "signal_grade=1 (snr_db=3, rssi_dbm=-80)") ==
        "unknown pulses=897 duration_ms=910 truncated=yes signal_grade=1 (snr_db=3, rssi_dbm=-80)");
  CHECK(format_unknown(130, 76, false, "") == "unknown pulses=130 duration_ms=76 truncated=no");

  // Oregon quality fields carry their units.
  OregonSniffer oregon;
  std::vector<Decoded> out;
  std::vector<uint8_t> ok = {0x1A, 0x2D, 0x10, 0x7B, 0x10, 0x09, 0x80, 0xC9, 0x4A, 0xCC};
  CHECK(oregon.decode(encode_v21(ok), out) == 1 && out[0].quality == "short_us=488 long_us=976 pair_errors=0");
}

static void test_format() {
  CHECK(format_wall_time(9, 40, 28, 859) == "09:40:28.859");
  CHECK(format_uptime(1234567) == "up 1234.567");
  CHECK(format_uptime(5) == "up 0.005");
  std::vector<int32_t> raw;
  for (int i = 1; i <= 70; i++)
    raw.push_back(i % 2 ? i : -i);
  auto lines = format_raw_lines(raw, 32);
  CHECK(lines.size() == 3);
  CHECK(lines.size() == 3 && lines[0].rfind("1, -2, 3", 0) == 0 && lines[2] == "65, -66, 67, -68, 69, -70");
  CHECK(capture_duration_us({100, -200, 300}) == 600);
  CHECK(std::string(decoder_name(Decoder::RC_SWITCH)) == "rc_switch");
}

int main(int argc, char **argv) {
  const auto caps = load(argc > 1 ? argv[1] : "data/cc1101_frames.txt");
  CHECK(caps.size() == 10);
  test_oregon_real_captures(caps);
  test_oregon_unknown_type();
  test_oregon_rejected();
  test_repeats();
  test_rssi();
  test_signal_and_lines();
  test_format();
  std::printf(failures == 0 ? "CORE TESTS PASSED\n" : "%d CORE FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}
