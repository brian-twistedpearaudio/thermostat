// Unit tests for the control and schedule logic. Run with: pio test -e native
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "control.h"
#include "graph_data.h"
#include "log_cache.h"
#include "log_interval.h"
#include "schedule.h"
#include "settings.h"
#include "sync_text.h"
#include "upload_text.h"
#include "wifi_text.h"

using namespace thermo;

void setUp() {}
void tearDown() {}

// ---------- Controller ----------

static void test_no_heat_until_min_off_after_boot() {
  Controller c;  // set 68, swing 1.0, min on 120, min off 180
  TEST_ASSERT_FALSE(c.update(60.0f, true, 0));
  TEST_ASSERT_FALSE(c.update(60.0f, true, 179));
  TEST_ASSERT_TRUE(c.update(60.0f, true, 180));
}

static void test_swing_band() {
  Controller c;
  c.setSetpoint(70.0f);
  TEST_ASSERT_FALSE(c.update(69.6f, true, 1000));  // inside band, stays off
  TEST_ASSERT_TRUE(c.update(69.5f, true, 1010));   // on at set - 0.5
  TEST_ASSERT_TRUE(c.update(70.4f, true, 2000));   // inside band, stays on
  TEST_ASSERT_FALSE(c.update(70.5f, true, 2010));  // off at set + 0.5
  TEST_ASSERT_FALSE(c.update(70.0f, true, 3000));  // inside band, stays off
}

static void test_min_on_time() {
  Controller c;
  c.setSetpoint(70.0f);
  TEST_ASSERT_TRUE(c.update(65.0f, true, 1000));
  TEST_ASSERT_TRUE(c.update(75.0f, true, 1119));   // too soon to turn off
  TEST_ASSERT_FALSE(c.update(75.0f, true, 1120));
}

static void test_min_off_time() {
  Controller c;
  c.setSetpoint(70.0f);
  TEST_ASSERT_TRUE(c.update(65.0f, true, 1000));
  TEST_ASSERT_FALSE(c.update(75.0f, true, 2000));
  TEST_ASSERT_FALSE(c.update(65.0f, true, 2179));  // too soon to turn back on
  TEST_ASSERT_TRUE(c.update(65.0f, true, 2180));
}

static void test_sensor_fault_forces_off_immediately() {
  Controller c;
  TEST_ASSERT_TRUE(c.update(60.0f, true, 1000));
  TEST_ASSERT_FALSE(c.update(60.0f, false, 1001));  // ignores min on time
  TEST_ASSERT_FALSE(c.update(60.0f, true, 1100));   // min off still applies
  TEST_ASSERT_TRUE(c.update(60.0f, true, 1181));
}

static void test_mode_off_forces_off() {
  Controller c;
  TEST_ASSERT_TRUE(c.update(60.0f, true, 1000));
  c.setMode(Mode::Off);
  TEST_ASSERT_FALSE(c.update(60.0f, true, 1001));
  TEST_ASSERT_FALSE(c.update(60.0f, true, 5000));
}

static void test_wider_swing_and_limits() {
  ControlConfig cfg;
  cfg.swingF = 3.0f;
  Controller c(cfg);
  c.setSetpoint(70.0f);
  TEST_ASSERT_FALSE(c.update(68.6f, true, 1000));
  TEST_ASSERT_TRUE(c.update(68.5f, true, 1010));
  cfg.swingF = 10.0f;
  c.setConfig(cfg);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, kMaxSwingF, c.config().swingF);
  c.setSetpoint(120.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, kMaxSetpointF, c.setpoint());
}

// ---------- Schedule ----------

static const ScheduleEntry kDay[] = {{22 * 60, 62.0f}, {6 * 60, 68.0f}, {17 * 60, 70.0f}};

static void test_schedule_sorts_and_validates() {
  Schedule s;
  TEST_ASSERT_TRUE(s.set(kDay, 3));
  TEST_ASSERT_EQUAL_INT(3, (int)s.count());
  TEST_ASSERT_EQUAL_INT(6 * 60, s.entry(0).minuteOfDay);
  TEST_ASSERT_EQUAL_INT(22 * 60, s.entry(2).minuteOfDay);

  const ScheduleEntry badTime[] = {{1440, 68.0f}};
  const ScheduleEntry badTemp[] = {{60, 100.0f}};
  const ScheduleEntry dup[] = {{60, 68.0f}, {60, 70.0f}};
  TEST_ASSERT_FALSE(s.set(badTime, 1));
  TEST_ASSERT_FALSE(s.set(badTemp, 1));
  TEST_ASSERT_FALSE(s.set(dup, 2));
  TEST_ASSERT_EQUAL_INT(3, (int)s.count());  // unchanged after rejects
}

static void test_schedule_active_index_wraps() {
  Schedule s;
  TEST_ASSERT_EQUAL_INT(-1, s.activeIndex(0));
  s.set(kDay, 3);
  TEST_ASSERT_EQUAL_INT(2, s.activeIndex(3 * 60));   // 3 AM: last night's entry
  TEST_ASSERT_EQUAL_INT(0, s.activeIndex(6 * 60));
  TEST_ASSERT_EQUAL_INT(1, s.activeIndex(18 * 60));
  TEST_ASSERT_EQUAL_INT(2, s.activeIndex(23 * 60));
}

static void test_setpoint_follows_schedule() {
  Schedule s;
  s.set(kDay, 3);
  SetpointManager m(65.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 65.0f, m.update(s, false, 0));  // no time yet
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 68.0f, m.update(s, true, 7 * 60));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 70.0f, m.update(s, true, 17 * 60));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 62.0f, m.update(s, true, 2 * 60));
}

static void test_manual_change_lasts_until_next_event() {
  Schedule s;
  s.set(kDay, 3);
  SetpointManager m;
  m.update(s, true, 7 * 60);
  m.manualSet(72.0f);
  TEST_ASSERT_TRUE(m.overridden());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 72.0f, m.update(s, true, 16 * 60 + 59));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 70.0f, m.update(s, true, 17 * 60));
  TEST_ASSERT_FALSE(m.overridden());
}

static void test_manual_change_across_midnight_and_single_entry() {
  const ScheduleEntry one[] = {{6 * 60, 68.0f}};
  Schedule s;
  s.set(one, 1);
  SetpointManager m;
  m.update(s, true, 23 * 60);
  m.manualSet(64.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 64.0f, m.update(s, true, 23 * 60 + 59));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 64.0f, m.update(s, true, 1));       // past midnight
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 68.0f, m.update(s, true, 6 * 60));  // next event
}

static void test_manual_change_before_time_is_known() {
  Schedule s;
  s.set(kDay, 3);
  SetpointManager m;
  m.manualSet(66.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 66.0f, m.update(s, false, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 66.0f, m.update(s, true, 7 * 60));  // kept
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 70.0f, m.update(s, true, 17 * 60));
}

// ---------- Settings ----------

static void test_settings_sanitize() {
  Settings s;
  s.setpointF = 500;
  s.swingTenths = 0;
  s.offsetTenths = -999;
  s.brightnessPct = 0;
  s.dimSeconds = 7;
  sanitize(s);
  TEST_ASSERT_EQUAL_INT(85, s.setpointF);
  TEST_ASSERT_EQUAL_INT(5, s.swingTenths);
  TEST_ASSERT_EQUAL_INT(-100, s.offsetTenths);
  TEST_ASSERT_EQUAL_INT(10, s.brightnessPct);  // never black
  TEST_ASSERT_EQUAL_INT(60, s.dimSeconds);
  s.swingTenths = 17;
  s.dimSeconds = 0;
  sanitize(s);
  TEST_ASSERT_EQUAL_INT(15, s.swingTenths);
  TEST_ASSERT_EQUAL_INT(0, s.dimSeconds);  // 0 = never is valid
}

static void test_settings_step_limits() {
  Settings s;
  TEST_ASSERT_TRUE(stepSetting(s, SettingId::Mode, 1));
  TEST_ASSERT_FALSE(s.heatMode);
  for (int i = 0; i < 300; ++i) {
    stepSetting(s, SettingId::Swing, 1);
    stepSetting(s, SettingId::Offset, -1);
    stepSetting(s, SettingId::Brightness, -1);
    stepSetting(s, SettingId::DimTimeout, 1);
  }
  TEST_ASSERT_EQUAL_INT(30, s.swingTenths);
  TEST_ASSERT_EQUAL_INT(-100, s.offsetTenths);
  TEST_ASSERT_EQUAL_INT(10, s.brightnessPct);
  TEST_ASSERT_EQUAL_INT(0, s.dimSeconds);
  TEST_ASSERT_FALSE(stepSetting(s, SettingId::Swing, 1));  // no change at the limit
  TEST_ASSERT_TRUE(stepSetting(s, SettingId::DimTimeout, -1));
  TEST_ASSERT_EQUAL_INT(300, s.dimSeconds);
}

static void test_settings_text() {
  Settings s;
  char b[16];
  s.offsetTenths = -5;
  formatSetting(s, SettingId::Offset, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("-0.5\xC2\xB0", b);
  formatSetting(s, SettingId::Swing, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1.0\xC2\xB0", b);
  formatSetting(s, SettingId::DimTimeout, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1 min", b);
  s.dimSeconds = 0;
  formatSetting(s, SettingId::DimTimeout, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("Never", b);
}

// ---------- Wi-Fi setup ----------

static void test_wifi_validation() {
  TEST_ASSERT_TRUE(validWifi("Home", "12345678"));
  TEST_ASSERT_TRUE(validWifi("Open network", ""));
  TEST_ASSERT_TRUE(validWifi(std::string(32, 'a'), std::string(63, 'b')));
  TEST_ASSERT_FALSE(validWifi("", "12345678"));
  TEST_ASSERT_FALSE(validWifi(std::string(33, 'a'), "12345678"));
  TEST_ASSERT_FALSE(validWifi("Home", "1234567"));
  TEST_ASSERT_FALSE(validWifi("Home", std::string(64, 'b')));
  TEST_ASSERT_FALSE(validWifi("Home", "12345678\n"));
}

static void test_clean_name() {
  std::string n = "  Living Room ";
  TEST_ASSERT_TRUE(cleanName(n));
  TEST_ASSERT_EQUAL_STRING("Living Room", n.c_str());
  n = "Sam\xE2\x80\x99s Room";  // curly apostrophe
  TEST_ASSERT_TRUE(cleanName(n));
  TEST_ASSERT_EQUAL_STRING("Sam's Room", n.c_str());
  n = "   ";
  TEST_ASSERT_TRUE(cleanName(n));
  TEST_ASSERT_TRUE(n.empty());
  n = std::string(16, 'a');
  TEST_ASSERT_TRUE(cleanName(n));
  n = std::string(17, 'a');
  TEST_ASSERT_FALSE(cleanName(n));
  n = "Caf\xC3\xA9";
  TEST_ASSERT_FALSE(cleanName(n));
  n = "a\nb";
  TEST_ASSERT_FALSE(cleanName(n));
}

static void test_html_escape() {
  TEST_ASSERT_EQUAL_STRING("&lt;b&gt;&quot;A&amp;B&#39;s&quot;", htmlEscape("<b>\"A&B's\"").c_str());
}

// ---------- 5-minute records ----------

static void test_log_interval_averages() {
  LogInterval iv;
  LogRecord rec;
  LogSample s;
  s.ok = true;
  s.setpointF = 70;
  const uint32_t t0 = 1700000100;  // a multiple of 300
  for (uint32_t i = 0; i < 300; ++i) {
    s.tempF = i < 150 ? 68.0f : 70.0f;
    s.heating = i < 75;
    TEST_ASSERT_FALSE(iv.add(t0 + i, 1000, s, rec));
  }
  s.modeOff = true;  // belongs to the next interval
  TEST_ASSERT_TRUE(iv.add(t0 + 300, 1000, s, rec));
  TEST_ASSERT_EQUAL_UINT32(t0, rec.time);
  TEST_ASSERT_TRUE(rec.hasTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 69.0f, rec.tempF);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 70.0f, rec.setpointF);
  TEST_ASSERT_EQUAL_INT(25, rec.heatPct);
  TEST_ASSERT_FALSE(rec.hasHumidity);
  TEST_ASSERT_FALSE(rec.sensorFault);
  TEST_ASSERT_FALSE(rec.modeOff);
  TEST_ASSERT_FALSE(rec.partial);
}

static void test_log_interval_fault_and_partial() {
  LogInterval iv;
  LogRecord rec;
  LogSample s;  // sensor not ok
  for (uint32_t i = 200; i < 300; ++i) TEST_ASSERT_FALSE(iv.add(1700000100 + i, 1000, s, rec));
  TEST_ASSERT_TRUE(iv.add(1700000400, 1000, s, rec));
  TEST_ASSERT_FALSE(rec.hasTemp);
  TEST_ASSERT_TRUE(rec.sensorFault);
  TEST_ASSERT_TRUE(rec.partial);
}

// ---------- Upload cache ----------

class RamFlash : public LogFlash {
 public:
  explicit RamFlash(uint32_t sectors) : mem(sectors * LogCache::kSector, 0x5A) {}  // starts as junk
  bool read(uint32_t addr, void* buf, size_t n) override {
    memcpy(buf, &mem[addr], n);
    return true;
  }
  bool write(uint32_t addr, const void* buf, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    for (size_t i = 0; i < n; ++i) mem[addr + i] &= p[i];  // like flash: can only clear bits
    return true;
  }
  bool erase(uint32_t addr) override {
    memset(&mem[addr], 0xFF, LogCache::kSector);
    return true;
  }
  std::vector<uint8_t> mem;
};

static LogRecord recordAt(uint32_t n) {
  LogRecord r;
  r.time = 1700000100u + n * kLogIntervalS;
  r.hasTemp = true;
  r.tempF = 70.25f;
  r.pressureHpa = 1001.2f;
  r.setpointF = 68;
  r.heatPct = 40;
  r.partial = true;
  return r;
}

static void test_cache_keeps_and_sends_in_order() {
  RamFlash f(3);
  LogCache c;
  TEST_ASSERT_TRUE(c.begin(f, 3));
  TEST_ASSERT_EQUAL_UINT32(0, c.pending());
  for (uint32_t i = 0; i < 10; ++i) TEST_ASSERT_TRUE(c.append(recordAt(i)));

  std::vector<LogRecord> got = c.peek(4);
  TEST_ASSERT_EQUAL_UINT32(4, got.size());
  TEST_ASSERT_EQUAL_UINT32(recordAt(0).time, got[0].time);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 70.25f, got[0].tempF);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1001.2f, got[0].pressureHpa);
  TEST_ASSERT_FALSE(got[0].hasHumidity);
  TEST_ASSERT_EQUAL_INT(40, got[0].heatPct);
  TEST_ASSERT_TRUE(got[0].partial);
  TEST_ASSERT_FALSE(got[0].modeOff);

  // Not marked: the same ones again. Marked: the next ones, also after a restart.
  TEST_ASSERT_EQUAL_UINT32(recordAt(0).time, c.peek(4)[0].time);
  TEST_ASSERT_TRUE(c.markSent());
  TEST_ASSERT_EQUAL_UINT32(6, c.pending());
  LogCache again;
  TEST_ASSERT_TRUE(again.begin(f, 3));
  TEST_ASSERT_EQUAL_UINT32(6, again.pending());
  TEST_ASSERT_TRUE(again.append(recordAt(10)));
  got = again.peek(100);
  TEST_ASSERT_EQUAL_UINT32(7, got.size());
  TEST_ASSERT_EQUAL_UINT32(recordAt(4).time, got.front().time);
  TEST_ASSERT_EQUAL_UINT32(recordAt(10).time, got.back().time);
  TEST_ASSERT_TRUE(again.markSent());
  TEST_ASSERT_EQUAL_UINT32(0, again.pending());
  TEST_ASSERT_EQUAL_UINT32(0, again.peek(100).size());
}

static void test_cache_drops_oldest_when_full() {
  RamFlash f(3);
  LogCache c;
  c.begin(f, 3);
  const uint32_t n = LogCache::kPerSector * 4 + 5;  // drops the first two sectors
  for (uint32_t i = 0; i < n; ++i) TEST_ASSERT_TRUE(c.append(recordAt(i)));
  for (int pass = 0; pass < 2; ++pass) {
    TEST_ASSERT_EQUAL_UINT32(LogCache::kPerSector * 2 + 5, c.pending());
    const std::vector<LogRecord> got = c.peek(n);
    TEST_ASSERT_EQUAL_UINT32(c.pending(), got.size());
    TEST_ASSERT_EQUAL_UINT32(recordAt(LogCache::kPerSector * 2).time, got.front().time);
    TEST_ASSERT_EQUAL_UINT32(recordAt(n - 1).time, got.back().time);
    c.begin(f, 3);  // the same after a restart
  }
}

static void test_cache_power_cuts() {
  RamFlash f(3);
  LogCache c;
  c.begin(f, 3);
  for (uint32_t i = 0; i < 5; ++i) c.append(recordAt(i));
  f.mem[6 * LogCache::kRecord] = 0x12;  // the sixth record only started
  c.begin(f, 3);
  TEST_ASSERT_EQUAL_UINT32(5, c.pending());
  TEST_ASSERT_TRUE(c.append(recordAt(6)));
  TEST_ASSERT_EQUAL_UINT32(6, c.peek(100).size());

  while (c.pending() < LogCache::kPerSector - 1) c.append(recordAt(c.pending() + 1));
  f.erase(LogCache::kSector);  // next sector erased, then power lost before its header
  c.begin(f, 3);
  TEST_ASSERT_EQUAL_UINT32(LogCache::kPerSector - 1, c.pending());
  TEST_ASSERT_TRUE(c.append(recordAt(1000)));
  TEST_ASSERT_EQUAL_UINT32(LogCache::kPerSector, c.peek(1000).size());
}

static void test_server_text() {
  std::string s = " http://192.168.1.5/ ";
  TEST_ASSERT_TRUE(cleanServer(s));
  TEST_ASSERT_EQUAL_STRING("192.168.1.5:5080", s.c_str());
  s = "my-pc.local:80";
  TEST_ASSERT_TRUE(cleanServer(s));
  TEST_ASSERT_EQUAL_STRING("my-pc.local:80", s.c_str());
  s = "";
  TEST_ASSERT_TRUE(cleanServer(s));
  for (const char* bad : {"a b", "host:", "host:0", "host:70000", "host:8x", ":80", "host/path", "https://host"}) {
    s = bad;
    TEST_ASSERT_FALSE(cleanServer(s));
  }
  TEST_ASSERT_TRUE(validToken("abcdefgh23456789"));
  TEST_ASSERT_FALSE(validToken("short"));
  TEST_ASSERT_FALSE(validToken("abcdefgh2345678\n"));
}

static void test_upload_json() {
  LogRecord r = recordAt(0);
  LogRecord none;
  none.time = 1700000400;
  TEST_ASSERT_EQUAL_STRING(
      "{\"device\":\"a1b2c3d4e5f6\",\"name\":\"The \\\"Den\\\"\",\"readings\":["
      "{\"time\":1700000100,\"tempF\":70.25,\"humidity\":null,\"pressureHpa\":1001.2,\"setpointF\":68.0,"
      "\"heatPct\":40,\"sensorFault\":false,\"modeOff\":false,\"partial\":true},"
      "{\"time\":1700000400,\"tempF\":null,\"humidity\":null,\"pressureHpa\":null,\"setpointF\":0.0,"
      "\"heatPct\":0,\"sensorFault\":false,\"modeOff\":false,\"partial\":false}]}",
      uploadJson("a1b2c3d4e5f6", "The \"Den\"", {r, none}).c_str());
}

static void test_graph_parse() {
  const GraphSpan span = graphSpan(1700000400 + 299, 300);
  TEST_ASSERT_EQUAL_UINT32(1700000400 + 300, span.end());
  static GraphPoint p[kGraphPoints];
  TEST_ASSERT_TRUE(parseGraph("graph 1\n1700000400,701,699,704,680\n1700000100,,,,-5\n1600000000,1,1,1,1\n", span, p));
  TEST_ASSERT_EQUAL_INT(701, p[kGraphPoints - 1].avg);
  TEST_ASSERT_EQUAL_INT(699, p[kGraphPoints - 1].min);
  TEST_ASSERT_EQUAL_INT(704, p[kGraphPoints - 1].max);
  TEST_ASSERT_EQUAL_INT(680, p[kGraphPoints - 1].set);
  TEST_ASSERT_EQUAL_INT(kGraphNone, p[kGraphPoints - 2].avg);
  TEST_ASSERT_EQUAL_INT(-5, p[kGraphPoints - 2].set);
  TEST_ASSERT_EQUAL_INT(kGraphNone, p[0].set);
  TEST_ASSERT_TRUE(parseGraph("graph 1\n", span, p));
  for (const char* bad : {"", "<html>", "graph 1\n1700000400,701", "graph 1\n1700000400,701,699,704,680",
                          "graph 1\n1700000400,x,1,1,1\n", "graph 1\n1700000400,9999,1,1,1\n",
                          "graph 1\n1700000400,1,1,1,\n"}) {
    TEST_ASSERT_FALSE(parseGraph(bad, span, p));
  }
}

static void test_graph_scale() {
  static GraphPoint p[kGraphPoints];
  int lo = 0, hi = 0, step = 0;
  for (GraphPoint& x : p) x = GraphPoint();
  TEST_ASSERT_FALSE(graphScale(p, lo, hi, step));
  p[3].set = 680;  // a set point alone is enough
  TEST_ASSERT_TRUE(graphScale(p, lo, hi, step));
  TEST_ASSERT_TRUE(lo <= 660 && hi >= 700);
  TEST_ASSERT_EQUAL_INT(10, step);
  p[4].min = 500;
  p[5].max = 760;
  TEST_ASSERT_TRUE(graphScale(p, lo, hi, step));
  TEST_ASSERT_TRUE(lo < 500 && hi > 760);
  TEST_ASSERT_EQUAL_INT(100, step);
}

static SyncFields syncExample() {
  SyncFields f;
  const char* values[] = {"70", "heat", "10", "-5", "0600=68,2200=62", "The Den"};
  for (int i = 0; i < kSyncCount; ++i) {
    f.value[i] = values[i];
    f.time[i] = 1795640000 + i;
  }
  return f;
}

static void test_sync_values() {
  const SyncField f[] = {kSyncSet, kSyncSet, kSyncMode, kSyncSwing, kSyncOffset, kSyncOffset, kSyncSchedule,
                         kSyncSchedule, kSyncName, kSyncName};
  const char* good[] = {"45", "85", "off", "30", "-100", "0", "-", "0000=45,0001=46,0002=47,0003=48,0004=49,2359=85",
                        "", "0123456789abcdef"};
  for (int i = 0; i < 10; ++i) TEST_ASSERT_TRUE_MESSAGE(validSyncValue(f[i], good[i]), good[i]);
  for (const char* v : {"", "44", "86", "070", "+70", "70.5", "70 ", "x"}) TEST_ASSERT_FALSE(validSyncValue(kSyncSet, v));
  for (const char* v : {"", "Heat", "cool"}) TEST_ASSERT_FALSE(validSyncValue(kSyncMode, v));
  for (const char* v : {"0", "7", "35", "05"}) TEST_ASSERT_FALSE(validSyncValue(kSyncSwing, v));
  for (const char* v : {"-101", "101", "-0", "-"}) TEST_ASSERT_FALSE(validSyncValue(kSyncOffset, v));
  for (const char* v : {"", "600=68", "2400=68", "0660=68", "0600=44", "0600=68,", ",0600=68", "0600=68,0600=70",
                        "0700=68,0600=70", "0600=", "0600=68, 0700=70",
                        "0000=45,0001=46,0002=47,0003=48,0004=49,0005=50,0006=51"}) {
    TEST_ASSERT_FALSE_MESSAGE(validSyncValue(kSyncSchedule, v), v);
  }
  for (const char* v : {" a", "a ", "0123456789abcdefg", "a\tb", "caf\xC3\xA9"}) TEST_ASSERT_FALSE(validSyncValue(kSyncName, v));
  Schedule s;
  TEST_ASSERT_TRUE(parseSchedule("0600=68,2230=62", s));
  TEST_ASSERT_EQUAL_UINT(2, s.count());
  TEST_ASSERT_EQUAL_UINT(22 * 60 + 30, s.entry(1).minuteOfDay);
  TEST_ASSERT_EQUAL_FLOAT(62, s.entry(1).setpointF);
  TEST_ASSERT_FALSE(parseSchedule("bad", s));
  TEST_ASSERT_EQUAL_UINT(2, s.count());
}

static void test_sync_request() {
  SyncFields f = syncExample();
  f.time[kSyncSet] = kSyncUnknown;
  SyncState st;
  st.setpointF = 70;
  TEST_ASSERT_EQUAL_STRING(
      "sync 1\nset -1 70\nmode 1795640001 heat\nswing 1795640002 10\noffset 1795640003 -5\n"
      "schedule 1795640004 0600=68,2200=62\nname 1795640005 The Den\nstate - 700 0 0\nend\n",
      syncRequest(f, st).c_str());
  f.value[kSyncName] = "";
  st.sensorOk = st.heating = st.manual = true;
  st.tempF = 72.14f;
  const std::string t = syncRequest(f, st);
  TEST_ASSERT_TRUE(t.find("name 1795640005 \nstate 721 700 1 1\nend\n") != std::string::npos);
}

static void test_sync_reply() {
  const std::string good =
      "sync 1\nset 1795640000 71\nmode 0 off\nswing 5 15\noffset 6 0\nschedule 7 -\nname 8 My  Den\nend\n";
  SyncFields f = syncExample();
  TEST_ASSERT_TRUE(parseSyncReply(good, f));
  TEST_ASSERT_EQUAL_STRING("71", f.value[kSyncSet].c_str());
  TEST_ASSERT_EQUAL_STRING("off", f.value[kSyncMode].c_str());
  TEST_ASSERT_EQUAL_STRING("My  Den", f.value[kSyncName].c_str());
  TEST_ASSERT_TRUE(f.time[kSyncSet] == 1795640000 && f.time[kSyncName] == 8);

  for (size_t n = 0; n < good.size(); ++n) TEST_ASSERT_FALSE(parseSyncReply(good.substr(0, n), f));  // cut off
  TEST_ASSERT_FALSE(parseSyncReply(good + "x", f));
  const char* swaps[][2] = {{"sync 1", "sync 2"}, {" 71", " 99"}, {"set 1", "set -1"}, {"mode 0 off", "mode 0 on"},
                            {"swing 5", "swing x"}, {"offset 6", "offset  6"}, {"mode", "name"}, {"schedule 7 -", "schedule 7"}};
  for (auto& w : swaps) {
    std::string bad = good;
    bad.replace(bad.find(w[0]), strlen(w[0]), w[1]);
    TEST_ASSERT_FALSE_MESSAGE(parseSyncReply(bad, f), w[1]);
  }
  TEST_ASSERT_EQUAL_STRING("71", f.value[kSyncSet].c_str());  // unchanged by bad replies
}

static void test_sync_change_time() {
  TEST_ASSERT_TRUE(syncChangeTime(0, true, 1795640000) == 1795640000);
  TEST_ASSERT_TRUE(syncChangeTime(1795640000, true, 1795640000) == 1795640001);
  TEST_ASSERT_TRUE(syncChangeTime(1795640009, true, 1795640000) == 1795640010);
  TEST_ASSERT_TRUE(syncChangeTime(1795640000, false, 5) == kSyncUnknown);
  TEST_ASSERT_TRUE(syncChangeTime(kSyncUnknown, true, 1795640000) == 1795640000);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_heat_until_min_off_after_boot);
  RUN_TEST(test_swing_band);
  RUN_TEST(test_min_on_time);
  RUN_TEST(test_min_off_time);
  RUN_TEST(test_sensor_fault_forces_off_immediately);
  RUN_TEST(test_mode_off_forces_off);
  RUN_TEST(test_wider_swing_and_limits);
  RUN_TEST(test_schedule_sorts_and_validates);
  RUN_TEST(test_schedule_active_index_wraps);
  RUN_TEST(test_setpoint_follows_schedule);
  RUN_TEST(test_manual_change_lasts_until_next_event);
  RUN_TEST(test_manual_change_across_midnight_and_single_entry);
  RUN_TEST(test_manual_change_before_time_is_known);
  RUN_TEST(test_settings_sanitize);
  RUN_TEST(test_settings_step_limits);
  RUN_TEST(test_settings_text);
  RUN_TEST(test_wifi_validation);
  RUN_TEST(test_clean_name);
  RUN_TEST(test_html_escape);
  RUN_TEST(test_log_interval_averages);
  RUN_TEST(test_log_interval_fault_and_partial);
  RUN_TEST(test_cache_keeps_and_sends_in_order);
  RUN_TEST(test_cache_drops_oldest_when_full);
  RUN_TEST(test_cache_power_cuts);
  RUN_TEST(test_server_text);
  RUN_TEST(test_upload_json);
  RUN_TEST(test_graph_parse);
  RUN_TEST(test_graph_scale);
  RUN_TEST(test_sync_values);
  RUN_TEST(test_sync_request);
  RUN_TEST(test_sync_reply);
  RUN_TEST(test_sync_change_time);
  return UNITY_END();
}
