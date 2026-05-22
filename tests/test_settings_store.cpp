#include "unity.h"
#include "storage/settings_store.h"
#include "mocks/mock_preferences.h"
#include "config.h"

void setUp() {}
void tearDown() {}

void test_first_boot_writes_defaults() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin();
    // "init" key should be set
    TEST_ASSERT_TRUE(prefs.isKey("init"));
    TEST_ASSERT_EQUAL_INT32(1, prefs.getInt("init"));
    // Check a few default values were written
    TEST_ASSERT_TRUE(prefs.isKey("res"));
    TEST_ASSERT_EQUAL_INT32(DEFAULT_RESOLUTION, prefs.getInt("res"));
    TEST_ASSERT_TRUE(prefs.isKey("fps"));
    TEST_ASSERT_EQUAL_INT32(DEFAULT_FPS, prefs.getInt("fps"));
    TEST_ASSERT_TRUE(prefs.isKey("eis"));
    TEST_ASSERT_EQUAL_INT32(DEFAULT_EIS, prefs.getInt("eis"));
}

void test_load_returns_defaults_after_first_boot() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin();
    CameraSettings s = store.load();
    TEST_ASSERT_EQUAL_INT32(DEFAULT_RESOLUTION, s.resolution);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_FPS, s.fps);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_FOV, s.fov);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_VIDEO_FORMAT, s.videoFormat);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_EIS, s.eis);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_LOOP_RECORDING, s.loopRecording);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_AUTO_START_REC, s.autoStartRec);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_SHARPNESS, s.sharpness);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_EXPOSURE, s.exposure);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_WHITE_BALANCE, s.whiteBalance);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_CONTRAST, s.contrast);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_SATURATION, s.saturation);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_HUE, s.hue);
}

void test_save_and_reload_roundtrip() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin();
    CameraSettings saved{};
    saved.resolution = 0;    // 4K
    saved.fps = 1;           // 60fps
    saved.fov = 2;           // Narrow
    saved.videoFormat = 0;   // NTSC
    saved.eis = 0;
    saved.loopRecording = 1;
    saved.autoStartRec = 1;
    saved.sharpness = 0;
    saved.exposure = -2;
    saved.whiteBalance = 5;
    saved.contrast = 2;
    saved.saturation = 0;
    saved.hue = 180;
    TEST_ASSERT_TRUE(store.save(saved));
    CameraSettings loaded = store.load();
    TEST_ASSERT_EQUAL_INT32(saved.resolution, loaded.resolution);
    TEST_ASSERT_EQUAL_INT32(saved.fps, loaded.fps);
    TEST_ASSERT_EQUAL_INT32(saved.fov, loaded.fov);
    TEST_ASSERT_EQUAL_INT32(saved.videoFormat, loaded.videoFormat);
    TEST_ASSERT_EQUAL_INT32(saved.eis, loaded.eis);
    TEST_ASSERT_EQUAL_INT32(saved.loopRecording, loaded.loopRecording);
    TEST_ASSERT_EQUAL_INT32(saved.autoStartRec, loaded.autoStartRec);
    TEST_ASSERT_EQUAL_INT32(saved.sharpness, loaded.sharpness);
    TEST_ASSERT_EQUAL_INT32(saved.exposure, loaded.exposure);
    TEST_ASSERT_EQUAL_INT32(saved.whiteBalance, loaded.whiteBalance);
    TEST_ASSERT_EQUAL_INT32(saved.contrast, loaded.contrast);
    TEST_ASSERT_EQUAL_INT32(saved.saturation, loaded.saturation);
    TEST_ASSERT_EQUAL_INT32(saved.hue, loaded.hue);
}

void test_single_setting_save() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin();
    TEST_ASSERT_TRUE(store.saveSetting(SettingId::FPS, 1));
    CameraSettings s = store.load();
    TEST_ASSERT_EQUAL_INT32(1, s.fps);
    // Other settings should still be defaults
    TEST_ASSERT_EQUAL_INT32(DEFAULT_RESOLUTION, s.resolution);
}

void test_single_setting_out_of_range_rejected() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin();
    TEST_ASSERT_FALSE(store.saveSetting(SettingId::FPS, 99));
    // Should still be default
    CameraSettings s = store.load();
    TEST_ASSERT_EQUAL_INT32(DEFAULT_FPS, s.fps);
}

void test_reset_to_defaults() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin();
    store.saveSetting(SettingId::RESOLUTION, 0);
    store.saveSetting(SettingId::FPS, 3);
    store.resetToDefaults();
    CameraSettings s = store.load();
    TEST_ASSERT_EQUAL_INT32(DEFAULT_RESOLUTION, s.resolution);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_FPS, s.fps);
}

void test_second_boot_preserves_values() {
    MockPreferences prefs;
    SettingsStore store(prefs);
    store.begin(); // first boot
    store.saveSetting(SettingId::RESOLUTION, 0);
    // Simulate second boot — "init" key already exists
    SettingsStore store2(prefs);
    store2.begin();
    CameraSettings s = store2.load();
    TEST_ASSERT_EQUAL_INT32(0, s.resolution);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_boot_writes_defaults);
    RUN_TEST(test_load_returns_defaults_after_first_boot);
    RUN_TEST(test_save_and_reload_roundtrip);
    RUN_TEST(test_single_setting_save);
    RUN_TEST(test_single_setting_out_of_range_rejected);
    RUN_TEST(test_reset_to_defaults);
    RUN_TEST(test_second_boot_preserves_values);
    return UNITY_END();
}
