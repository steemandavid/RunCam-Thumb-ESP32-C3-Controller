#include "unity.h"
#include "flight/flight_controller.h"
#include "mocks/mock_camera.h"
#include "config.h"

void setUp() {}
void tearDown() {}

void test_idle_to_armed_on_arm_pin_low() {
    MockCamera camera;
    FlightController fc(camera);
    // ARM_PIN LOW for 100ms (> 50ms debounce)
    fc.update(0, false);  // not armed yet
    fc.update(10, true);  // start debounce
    fc.update(60, true);  // 50ms passed — should trigger
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    TEST_ASSERT_EQUAL_INT(1, camera.startRecordingCalled);
}

void test_armed_to_recording_on_success() {
    MockCamera camera;
    camera.startRecordingResult = CameraResult::OK;
    FlightController fc(camera);
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
}

void test_armed_to_idle_on_failure() {
    MockCamera camera;
    camera.startRecordingResult = CameraResult::ERROR_TIMEOUT;
    FlightController fc(camera);
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

void test_recording_to_stopping_on_timer() {
    MockCamera camera;
    FlightController fc(camera);
    // Arm
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());

    // Advance past auto-stop duration (started at t=60)
    uint32_t overtime = 60 + AUTO_STOP_DURATION_MS + 1;
    fc.update(overtime, true);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState()); // STOPPING → IDLE (restart=false)
    TEST_ASSERT_EQUAL_INT(1, camera.stopRecordingCalled);
}

void test_auto_stop_restart_false_goes_idle() {
    MockCamera camera;
    FlightController fc(camera);
    fc.setAutoRestart(false);
    // Arm
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    // Timer expires (started at t=60)
    fc.update(60 + AUTO_STOP_DURATION_MS + 1, true);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

void test_auto_stop_restart_true_goes_armed() {
    MockCamera camera;
    FlightController fc(camera);
    fc.setAutoRestart(true);
    // Arm
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    // Timer expires — stop then restart (started at t=60)
    fc.update(60 + AUTO_STOP_DURATION_MS + 1, true);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    TEST_ASSERT_EQUAL_INT(2, camera.startRecordingCalled); // once for arm, once for restart
    TEST_ASSERT_EQUAL_INT(1, camera.stopRecordingCalled);
}

void test_disarm_ignored_in_recording() {
    MockCamera camera;
    FlightController fc(camera);
    // Arm
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());

    // Disarm (ARM_PIN HIGH) — should be ignored
    fc.update(1000, false);
    fc.update(2000, false);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
}

void test_debounce_too_short_no_trigger() {
    MockCamera camera;
    FlightController fc(camera);
    fc.update(0, false);
    fc.update(10, true);
    fc.update(30, true); // only 20ms — not enough
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
    TEST_ASSERT_EQUAL_INT(0, camera.startRecordingCalled);
}

void test_force_start_stop_recording() {
    MockCamera camera;
    FlightController fc(camera);
    TEST_ASSERT_EQUAL(CameraResult::OK, fc.forceStartRecording());
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    TEST_ASSERT_EQUAL(CameraResult::OK, fc.forceStopRecording());
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

void test_force_start_surfaces_nak() {
    MockCamera camera;
    camera.startRecordingResult = CameraResult::REJECTED_STATE;
    FlightController fc(camera);
    CameraResult r = fc.forceStartRecording();
    TEST_ASSERT_EQUAL(CameraResult::REJECTED_STATE, r);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

// MAJOR-3: when the camera persistently NAKs stop, FlightController must
// back off (>= 1 s between attempts) and give up after AUTO_STOP_MAX_RETRIES.
void test_auto_stop_nak_backoff() {
    MockCamera camera;
    camera.stopRecordingResult = CameraResult::REJECTED_STATE;
    FlightController fc(camera);
    // Arm and reach RECORDING.
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());

    // First past-the-deadline tick — one stop attempt.
    uint32_t t = 60 + AUTO_STOP_DURATION_MS + 1;
    fc.update(t, true);
    TEST_ASSERT_EQUAL(1, camera.stopRecordingCalled);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());

    // Many ticks within the back-off window — no new attempts.
    for (uint32_t dt = 1; dt < FlightController::AUTO_STOP_RETRY_INTERVAL_MS; dt += 50) {
        fc.update(t + dt, true);
    }
    TEST_ASSERT_EQUAL(1, camera.stopRecordingCalled);

    // Past the back-off — second attempt.
    fc.update(t + FlightController::AUTO_STOP_RETRY_INTERVAL_MS + 1, true);
    TEST_ASSERT_EQUAL(2, camera.stopRecordingCalled);
}

void test_auto_stop_gives_up_after_max_retries() {
    MockCamera camera;
    camera.stopRecordingResult = CameraResult::REJECTED_STATE;
    FlightController fc(camera);
    fc.update(0, false);
    fc.update(10, true);
    fc.update(60, true);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());

    // Drive AUTO_STOP_MAX_RETRIES attempts, each ≥ AUTO_STOP_RETRY_INTERVAL_MS apart.
    uint32_t t = 60 + AUTO_STOP_DURATION_MS + 1;
    for (int i = 0; i < FlightController::AUTO_STOP_MAX_RETRIES; i++) {
        fc.update(t, true);
        t += FlightController::AUTO_STOP_RETRY_INTERVAL_MS + 10;
    }
    TEST_ASSERT_EQUAL(FlightController::AUTO_STOP_MAX_RETRIES, camera.stopRecordingCalled);
    // After max retries the controller forces back to IDLE rather than spinning forever.
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_to_armed_on_arm_pin_low);
    RUN_TEST(test_armed_to_recording_on_success);
    RUN_TEST(test_armed_to_idle_on_failure);
    RUN_TEST(test_recording_to_stopping_on_timer);
    RUN_TEST(test_auto_stop_restart_false_goes_idle);
    RUN_TEST(test_auto_stop_restart_true_goes_armed);
    RUN_TEST(test_disarm_ignored_in_recording);
    RUN_TEST(test_debounce_too_short_no_trigger);
    RUN_TEST(test_force_start_stop_recording);
    RUN_TEST(test_force_start_surfaces_nak);
    RUN_TEST(test_auto_stop_nak_backoff);
    RUN_TEST(test_auto_stop_gives_up_after_max_retries);
    return UNITY_END();
}
