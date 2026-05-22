#include "unity.h"
#include "camera/camera_settings.h"

void setUp() {}
void tearDown() {}

void test_valid_resolution_4k() {
    TEST_ASSERT_TRUE(isValidSettingValue(SettingId::RESOLUTION, 0));
}

void test_valid_fps_60() {
    TEST_ASSERT_TRUE(isValidSettingValue(SettingId::FPS, 1));
}

void test_invalid_resolution() {
    TEST_ASSERT_FALSE(isValidSettingValue(SettingId::RESOLUTION, 5));
}

void test_invalid_fps() {
    TEST_ASSERT_FALSE(isValidSettingValue(SettingId::FPS, 90));
}

void test_exposure_in_range() {
    TEST_ASSERT_TRUE(isValidSettingValue(SettingId::EXPOSURE, -2));
}

void test_exposure_out_of_range() {
    TEST_ASSERT_FALSE(isValidSettingValue(SettingId::EXPOSURE, -3));
}

void test_hue_in_range() {
    TEST_ASSERT_TRUE(isValidSettingValue(SettingId::HUE, 180));
}

void test_hue_out_of_range() {
    TEST_ASSERT_FALSE(isValidSettingValue(SettingId::HUE, 181));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_resolution_4k);
    RUN_TEST(test_valid_fps_60);
    RUN_TEST(test_invalid_resolution);
    RUN_TEST(test_invalid_fps);
    RUN_TEST(test_exposure_in_range);
    RUN_TEST(test_exposure_out_of_range);
    RUN_TEST(test_hue_in_range);
    RUN_TEST(test_hue_out_of_range);
    return UNITY_END();
}
