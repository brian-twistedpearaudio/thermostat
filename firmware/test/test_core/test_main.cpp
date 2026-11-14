// Unit tests for the control and schedule logic. Run with: pio test -e native
#include <unity.h>

#include "control.h"
#include "schedule.h"

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
  return UNITY_END();
}
