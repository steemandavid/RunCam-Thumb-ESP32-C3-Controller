#include "unity.h"
#include "flight/flight_controller.h"
#include "mocks/mock_camera.h"

void setUp() {}
void tearDown() {}

void test_force_start_transitions_to_recording() {
    MockCamera camera;
    FlightController fc(camera);
    TEST_ASSERT_EQUAL(CameraResult::OK, fc.forceStartRecording());
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    TEST_ASSERT_EQUAL_INT(1, camera.startRecordingCalled);
}

void test_force_start_surfaces_nak() {
    MockCamera camera;
    camera.startRecordingResult = CameraResult::REJECTED_STATE;
    FlightController fc(camera);
    CameraResult r = fc.forceStartRecording();
    TEST_ASSERT_EQUAL(CameraResult::REJECTED_STATE, r);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

void test_force_stop_transitions_to_idle() {
    MockCamera camera;
    FlightController fc(camera);
    fc.forceStartRecording();
    TEST_ASSERT_EQUAL(CameraResult::OK, fc.forceStopRecording());
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
    TEST_ASSERT_EQUAL_INT(1, camera.stopRecordingCalled);
}

void test_force_stop_surfaces_nak() {
    MockCamera camera;
    camera.stopRecordingResult = CameraResult::REJECTED_STATE;
    FlightController fc(camera);
    fc.forceStartRecording();
    CameraResult r = fc.forceStopRecording();
    TEST_ASSERT_EQUAL(CameraResult::REJECTED_STATE, r);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
}

void test_start_while_recording_is_noop() {
    MockCamera camera;
    FlightController fc(camera);
    fc.forceStartRecording();
    int before = camera.startRecordingCalled;
    CameraResult r = fc.forceStartRecording();
    TEST_ASSERT_EQUAL(CameraResult::OK, r);
    TEST_ASSERT_EQUAL_INT(before, camera.startRecordingCalled);
}

void test_stop_while_idle_is_noop() {
    MockCamera camera;
    FlightController fc(camera);
    CameraResult r = fc.forceStopRecording();
    TEST_ASSERT_EQUAL(CameraResult::OK, r);
    TEST_ASSERT_EQUAL_INT(0, camera.stopRecordingCalled);
}

void test_recording_persists_indefinitely() {
    MockCamera camera;
    FlightController fc(camera);
    fc.forceStartRecording();
    uint32_t startMs = 1000;
    // Simulate 30 minutes passing — state should remain RECORDING
    for (uint32_t t = startMs; t < startMs + 1800000; t += 60000) {
        fc.update(t);
    }
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    TEST_ASSERT_EQUAL_INT(0, camera.stopRecordingCalled);
}

void test_recording_seconds_tracked() {
    MockCamera camera;
    FlightController fc(camera);
    fc.forceStartRecording(10000);
    TEST_ASSERT_EQUAL(0, fc.getRecordingSeconds(10000));
    TEST_ASSERT_EQUAL(60, fc.getRecordingSeconds(70000));
}

void test_recording_seconds_zero_when_idle() {
    MockCamera camera;
    FlightController fc(camera);
    TEST_ASSERT_EQUAL(0, fc.getRecordingSeconds(60000));
}

void test_initial_state_is_idle() {
    MockCamera camera;
    FlightController fc(camera);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

// --- syncRecordingState() ---------------------------------------------------

void test_sync_idle_to_recording() {
    MockCamera camera;
    camera.recordingState = true;
    FlightController fc(camera);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
    fc.syncRecordingState(1000);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    TEST_ASSERT_EQUAL(0, fc.getRecordingSeconds(1000));
    TEST_ASSERT_EQUAL(5, fc.getRecordingSeconds(6000));
}

void test_sync_recording_to_idle() {
    MockCamera camera;
    FlightController fc(camera);
    fc.forceStartRecording(1000);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
    camera.recordingState = false;
    fc.syncRecordingState(5000);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
    TEST_ASSERT_EQUAL(0, fc.getRecordingSeconds(5000));
}

void test_sync_no_change_when_states_match_recording() {
    MockCamera camera;
    FlightController fc(camera);
    fc.forceStartRecording(1000);
    camera.recordingState = true;  // still recording
    fc.syncRecordingState(5000);
    TEST_ASSERT_EQUAL(FlightState::RECORDING, fc.getState());
}

void test_sync_no_change_when_states_match_idle() {
    MockCamera camera;
    camera.recordingState = false;
    FlightController fc(camera);
    fc.syncRecordingState(1000);
    TEST_ASSERT_EQUAL(FlightState::IDLE, fc.getState());
}

void test_sync_timer_starts_from_detection_time() {
    MockCamera camera;
    camera.recordingState = true;
    FlightController fc(camera);
    // Camera started at t=0, but we detect it at t=7000 via poll.
    fc.syncRecordingState(7000);
    TEST_ASSERT_EQUAL(0, fc.getRecordingSeconds(7000));
    TEST_ASSERT_EQUAL(3, fc.getRecordingSeconds(10000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_force_start_transitions_to_recording);
    RUN_TEST(test_force_start_surfaces_nak);
    RUN_TEST(test_force_stop_transitions_to_idle);
    RUN_TEST(test_force_stop_surfaces_nak);
    RUN_TEST(test_start_while_recording_is_noop);
    RUN_TEST(test_stop_while_idle_is_noop);
    RUN_TEST(test_recording_persists_indefinitely);
    RUN_TEST(test_recording_seconds_tracked);
    RUN_TEST(test_recording_seconds_zero_when_idle);
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_sync_idle_to_recording);
    RUN_TEST(test_sync_recording_to_idle);
    RUN_TEST(test_sync_no_change_when_states_match_recording);
    RUN_TEST(test_sync_no_change_when_states_match_idle);
    RUN_TEST(test_sync_timer_starts_from_detection_time);
    return UNITY_END();
}
