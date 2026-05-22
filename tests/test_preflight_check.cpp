#include "unity.h"
#include "flight/preflight_check.h"
#include "mocks/mock_camera.h"

void setUp() {}
void tearDown() {}

// PreflightCheck is in DEFERRED mode — see FSD §5.2 and
// Serial_Diagnostic_Report.md §2.4. It returns a synthetic
// "settings access not available" result without touching the camera.

void test_deferred_mode_passes() {
    MockCamera camera;
    CameraSettings expected{};
    expected.resolution = 0;
    expected.fps = 1;
    expected.eis = 1;
    PreflightResult result = PreflightCheck::run(camera, expected);
    TEST_ASSERT_TRUE(result.passed);
    TEST_ASSERT_TRUE(result.deferred);
    TEST_ASSERT_TRUE(result.commsOk);
}

void test_deferred_mode_does_not_call_camera() {
    MockCamera camera;
    camera.readSettingFail = true;  // would explode if actually called
    CameraSettings expected{};
    PreflightResult result = PreflightCheck::run(camera, expected);
    // Should still pass — preflight didn't ask the camera anything.
    TEST_ASSERT_TRUE(result.passed);
    TEST_ASSERT_TRUE(result.deferred);
    TEST_ASSERT_TRUE(result.commsOk);
    TEST_ASSERT_EQUAL_size_t(0, camera.readSettingCalls.size());
}

void test_deferred_result_reports_expected_values() {
    MockCamera camera;
    CameraSettings expected{};
    expected.resolution = 2;
    expected.fps = 3;
    expected.eis = 0;
    PreflightResult result = PreflightCheck::run(camera, expected);
    // The "actual" values mirror "expected" since we don't read anything.
    // The UI uses these to render — checks marked OK with consistent values.
    TEST_ASSERT_TRUE(result.resolution.ok);
    TEST_ASSERT_EQUAL_INT32(2, result.resolution.expected);
    TEST_ASSERT_EQUAL_INT32(2, result.resolution.actual);
    TEST_ASSERT_TRUE(result.fps.ok);
    TEST_ASSERT_TRUE(result.eis.ok);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_deferred_mode_passes);
    RUN_TEST(test_deferred_mode_does_not_call_camera);
    RUN_TEST(test_deferred_result_reports_expected_values);
    return UNITY_END();
}
