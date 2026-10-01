#include <unity.h>
#include <string.h>
#include "model/caps.h"
#include "model/fnc_settings.h"
#include "../host_stubs.h"

void setUp() {
    fncset_clear();
    caps_reset();
    stub_sent_reset();
    stub_set_millis(0);
}
void tearDown() {}

// The live Corgi's $S, trimmed to the keys caps reads. Captured from FluidNC
// v4.0.3 on 2026-08-25.
static void feed_bringup_settings() {
    const char* L[] = {
        "$Grbl/SoftLimitsEnable=0",
        "$Grbl/HardLimitsEnable=0",
        "$Grbl/HomingCycleEnable=0",
        "$Grbl/MaxSpindleSpeed=24000",
        "$Grbl/Resolution/X=39.895", "$Grbl/Resolution/Y=40.000",
        "$Grbl/Resolution/Z=800.000", "$Grbl/Resolution/A=8.889",
        "$Grbl/MaxRate/X=5000.000",  "$Grbl/MaxRate/Y=5000.000",
        "$Grbl/MaxRate/Z=5000.000",  "$Grbl/MaxRate/A=10000.000",
        "$Grbl/Acceleration/X=350.000", "$Grbl/Acceleration/Y=350.000",
        "$Grbl/Acceleration/Z=350.000", "$Grbl/Acceleration/A=200.000",
        "$Grbl/MaxTravel/X=1000.000", "$Grbl/MaxTravel/Y=800.000",
        "$Grbl/MaxTravel/Z=100.000",  "$Grbl/MaxTravel/A=360.000",
    };
    for (auto l : L) fncset_feed_line(l);
    caps_refresh_from_settings();
}

void test_bringup_machine_reports_no_homing() {
    feed_bringup_settings();
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_homing());
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_hard_limits());
    TEST_ASSERT_FALSE(cap_ok(caps_homing()));
}

void test_homing_enabled_is_present() {
    fncset_feed_line("$Grbl/HomingCycleEnable=1");
    caps_refresh_from_settings();
    TEST_ASSERT_EQUAL(CAP_PRESENT, caps_homing());
    TEST_ASSERT_TRUE(cap_ok(caps_homing()));
}

// Nothing reported must leave controls usable: a query that never answered is
// not evidence the machine lacks the hardware.
void test_unreported_capability_stays_usable() {
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_homing());
    TEST_ASSERT_TRUE(cap_ok(caps_homing()));
    TEST_ASSERT_TRUE(cap_ok(caps_probe()));
}

void test_axes_come_from_the_settings_dump() {
    feed_bringup_settings();
    TEST_ASSERT_EQUAL(4, caps_axis_count());
    TEST_ASSERT_EQUAL('X', caps_axis_letter(0));
    TEST_ASSERT_EQUAL('A', caps_axis_letter(3));
    TEST_ASSERT_EQUAL_FLOAT(39.895f,  caps_axis_steps(0));
    TEST_ASSERT_EQUAL_FLOAT(5000.0f,  caps_axis_rate(0));
    TEST_ASSERT_EQUAL_FLOAT(200.0f,   caps_axis_accel(3));
    TEST_ASSERT_EQUAL_FLOAT(360.0f,   caps_axis_travel(3));
}

void test_setting_names_round_trip_to_the_dump_keys() {
    feed_bringup_settings();
    TEST_ASSERT_EQUAL_STRING("Grbl/MaxRate/X",      caps_setting_for(CAPS_RATE, 0));
    TEST_ASSERT_EQUAL_STRING("Grbl/Acceleration/A", caps_setting_for(CAPS_ACCEL, 3));
    TEST_ASSERT_EQUAL_STRING("Grbl/Resolution/Z",   caps_setting_for(CAPS_STEPS, 2));
    // The name must match a key that actually came back, or a write would be
    // sent to a setting the controller does not have.
    TEST_ASSERT_NOT_NULL(fncset_at(0));
}

static void feed_config(const char* const* lines, int n) {
    caps_request_config();
    for (int i = 0; i < n; i++) caps_feed_line(lines[i]);
    caps_feed_line("ok");
}

void test_config_dump_finds_no_probe_on_the_bringup_machine() {
    const char* L[] = {
        "board: Corgi", "probe:", "  pin: NO_PIN", "  toolsetter_pin: NO_PIN",
        "coolant:", "  flood_pin: NO_PIN", "  mist_pin: NO_PIN",
        "control:", "  safety_door_pin: NO_PIN", "  estop_pin: NO_PIN",
    };
    feed_config(L, 10);
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_probe());
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_toolsetter());
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_flood());
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_safety_door());
}

void test_config_dump_finds_configured_pins() {
    const char* L[] = {
        "board: Corgi", "probe:", "  pin: gpio.26:low:pu", "  toolsetter_pin: gpio.4:low:pu",
        "coolant:", "  flood_pin: gpio.2", "  mist_pin: NO_PIN",
        "control:", "  safety_door_pin: gpio.39:low:pu", "  estop_pin: NO_PIN",
    };
    feed_config(L, 10);
    TEST_ASSERT_EQUAL(CAP_PRESENT, caps_probe());
    TEST_ASSERT_EQUAL(CAP_PRESENT, caps_toolsetter());
    TEST_ASSERT_EQUAL(CAP_PRESENT, caps_flood());
    TEST_ASSERT_EQUAL(CAP_ABSENT,  caps_mist());
    TEST_ASSERT_EQUAL(CAP_PRESENT, caps_safety_door());
    TEST_ASSERT_EQUAL(CAP_ABSENT,  caps_estop());
}

// A 'pin:' key means nothing without knowing which section it is under.
void test_pin_keys_are_scoped_to_their_section() {
    const char* L[] = {
        "spi:", "  cs_pin: gpio.5", "probe:", "  pin: NO_PIN",
    };
    feed_config(L, 4);
    TEST_ASSERT_EQUAL(CAP_ABSENT, caps_probe());
}

// The regression that made every capability unreadable: $CD and $S are queued
// together, so the scanner must not consume the settings reply, and must not
// treat another command's "ok" as the end of the config dump.
void test_scanner_never_eats_the_settings_reply() {
    caps_request_config();
    TEST_ASSERT_FALSE(caps_feed_line("$Grbl/HomingCycleEnable=0"));
    TEST_ASSERT_FALSE(caps_feed_line("[G54:0.000,0.000,0.000]"));
    TEST_ASSERT_FALSE(caps_feed_line("{\"files\":[],\"path\":\"/sd\"}"));
    TEST_ASSERT_FALSE(caps_feed_line("<Idle|MPos:0.000,0.000,0.000>"));
    TEST_ASSERT_FALSE(caps_feed_line("ok"));   // belongs to $S, not to $CD

    // The dump is still live, so the YAML that follows is still read.
    TEST_ASSERT_TRUE(caps_feed_line("probe:"));
    TEST_ASSERT_TRUE(caps_feed_line("  pin: gpio.26"));
    TEST_ASSERT_TRUE(caps_feed_line("ok"));
    TEST_ASSERT_EQUAL(CAP_PRESENT, caps_probe());
}

// Firmware without $CD must not leave the scanner armed forever.
// On the panel the parser consumes "ok" itself and reports it through
// caps_reply_end(); the scanner must then let plain lines go again. It used to
// stay in dump mode for the whole session and eat every reply after it.
void test_dump_ended_by_parser_ok_releases_later_lines() {
    caps_request_config();
    TEST_ASSERT_TRUE(caps_feed_line("name: corgi"));
    TEST_ASSERT_TRUE(caps_feed_line("probe:"));
    TEST_ASSERT_TRUE(caps_feed_line("  pin: NO_PIN"));
    caps_reply_end();                               // the ok, via show_ok()
    TEST_ASSERT_TRUE(caps_settled());
    TEST_ASSERT_FALSE(caps_feed_line("0 GPIO0"));   // $GD output reaches the console
    TEST_ASSERT_FALSE(caps_feed_line("Input Matrix"));
}

// An ok that arrives before the YAML starts belongs to an earlier command.
void test_ok_before_dump_starts_does_not_end_it() {
    caps_request_config();
    caps_reply_end();                               // $#'s ok, queued ahead of $CD
    TEST_ASSERT_FALSE(caps_settled());
    TEST_ASSERT_TRUE(caps_feed_line("probe:"));     // still collecting
}

void test_gpio_roles_come_from_the_dump() {
    caps_request_config();
    const char* L[] = {
        "axes:",
        "  x:",
        "    motor0:",
        "      limit_neg_pin: gpio.32:pu:low",
        "      hard_limits: false",
        "  z:",
        "    motor0:",
        "      limit_pos_pin: 'gpio.26:low'",   // quoted, as some writers emit
        "uart2:",
        "  txd_pin: gpio.27",
        "probe:",
        "  pin: NO_PIN",
    };
    for (const char* l : L) caps_feed_line(l);
    caps_reply_end();
    TEST_ASSERT_EQUAL_STRING("x/motor0/limit_neg (pu low)", caps_gpio_role(32));
    TEST_ASSERT_EQUAL_STRING("z/motor0/limit_pos (low)", caps_gpio_role(26));
    TEST_ASSERT_EQUAL_STRING("uart2/txd", caps_gpio_role(27));
    TEST_ASSERT_NULL(caps_gpio_role(13));              // unused -> "free" on Diag
}

void test_config_request_gives_up() {
    caps_request_config();
    stub_set_millis(20000);
    caps_poll();
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_probe());
    TEST_ASSERT_FALSE(caps_feed_line("probe:"));
}

void test_config_request_is_actually_sent() {
    caps_request_config();
    TEST_ASSERT_EQUAL(1, stub_sent_count());
    TEST_ASSERT_EQUAL_STRING("$CD", stub_sent(0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gpio_roles_come_from_the_dump);
    RUN_TEST(test_dump_ended_by_parser_ok_releases_later_lines);
    RUN_TEST(test_ok_before_dump_starts_does_not_end_it);
    RUN_TEST(test_bringup_machine_reports_no_homing);
    RUN_TEST(test_homing_enabled_is_present);
    RUN_TEST(test_unreported_capability_stays_usable);
    RUN_TEST(test_axes_come_from_the_settings_dump);
    RUN_TEST(test_setting_names_round_trip_to_the_dump_keys);
    RUN_TEST(test_config_dump_finds_no_probe_on_the_bringup_machine);
    RUN_TEST(test_config_dump_finds_configured_pins);
    RUN_TEST(test_pin_keys_are_scoped_to_their_section);
    RUN_TEST(test_scanner_never_eats_the_settings_reply);
    RUN_TEST(test_config_request_gives_up);
    RUN_TEST(test_config_request_is_actually_sent);
    return UNITY_END();
}
