#include "Copter.h"

#define ARM_DELAY               20  // called at 10hz so 2 seconds
#define DISARM_DELAY            20  // called at 10hz so 2 seconds
#define LOST_VEHICLE_DELAY      10  // called at 10hz so 1 second

static uint32_t auto_disarm_begin;

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
#define RTT_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
volatile uint32_t rtt_dbg_copter_motors_total_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_total_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_total_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_total_slow_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_calc_pwm_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_calc_pwm_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_calc_pwm_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_calc_pwm_slow_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_cork_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_cork_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_cork_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_output_ch_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_output_ch_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_output_ch_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_output_ch_slow_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_interlock_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_interlock_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_interlock_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_flightmode_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_flightmode_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_flightmode_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_flightmode_slow_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_push_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_push_accum_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_push_max_us RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_push_slow_count RTT_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_copter_motors_main_calls RTT_DBG_DTCM_BSS;

static void rtt_dbg_update_max(volatile uint32_t &slot, uint32_t value)
{
    if (value > slot) {
        slot = value;
    }
}
#endif

// auto_disarm_check - disarms the copter if it has been sitting on the ground in manual mode with throttle low for at least 15 seconds
void Copter::auto_disarm_check()
{
    uint32_t tnow_ms = millis();
    uint32_t disarm_delay_ms = 1000*constrain_int16(g.disarm_delay, 0, 127);

    // exit immediately if we are already disarmed, or if auto
    // disarming is disabled
    if (!motors->armed() || disarm_delay_ms == 0 || flightmode->mode_number() == Mode::Number::THROW) {
        auto_disarm_begin = tnow_ms;
        return;
    }

    // if the rotor is still spinning, don't initiate auto disarm
    if (motors->get_spool_state() > AP_Motors::SpoolState::GROUND_IDLE) {
        auto_disarm_begin = tnow_ms;
        return;
    }

    // always allow auto disarm if using interlock switch or motors are Emergency Stopped
    if ((ap.using_interlock && !motors->get_interlock()) || SRV_Channels::get_emergency_stop()) {
#if FRAME_CONFIG != HELI_FRAME
        // use a shorter delay if using throttle interlock switch or Emergency Stop, because it is less
        // obvious the copter is armed as the motors will not be spinning
        disarm_delay_ms /= 2;
#endif
    } else {
        bool sprung_throttle_stick = (g.throttle_behavior & THR_BEHAVE_FEEDBACK_FROM_MID_STICK) != 0;
        bool thr_low;
        if (flightmode->has_manual_throttle() || !sprung_throttle_stick) {
            thr_low = ap.throttle_zero;
        } else {
            float deadband_top = get_throttle_mid() + g.throttle_deadzone;
            thr_low = channel_throttle->get_control_in() <= deadband_top;
        }

        if (!thr_low || !ap.land_complete) {
            // reset timer
            auto_disarm_begin = tnow_ms;
        }
    }

    // disarm once timer expires
    if ((tnow_ms-auto_disarm_begin) >= disarm_delay_ms) {
        arming.disarm(AP_Arming::Method::DISARMDELAY);
        auto_disarm_begin = tnow_ms;
    }
}

// motors_output - send output to motors library which will adjust and send to ESCs and servos
// full_push is true when slower rate updates (e.g. servo output) need to be performed at the main loop rate.
void Copter::motors_output(bool full_push)
{
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    const uint32_t rtt_dbg_start_us = AP_HAL::micros();
    uint32_t rtt_dbg_stage_us = rtt_dbg_start_us;
#endif

#if AP_COPTER_ADVANCED_FAILSAFE_ENABLED
    // this is to allow the failsafe module to deliberately crash
    // the vehicle. Only used in extreme circumstances to meet the
    // OBC rules
    if (g2.afs.should_crash_vehicle()) {
        g2.afs.terminate_vehicle();
        if (!g2.afs.terminating_vehicle_via_landing()) {
            return;
        }
        // landing must continue to run the motors output
    }
#endif

    // Update arming delay state
    if (ap.in_arming_delay && (!motors->armed() || millis()-arm_time_ms > ARMING_DELAY_SEC*1.0e3f || flightmode->mode_number() == Mode::Number::THROW)) {
        ap.in_arming_delay = false;
    }

    // output any servo channels
    SRV_Channels::calc_pwm();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    uint32_t rtt_dbg_now_us = AP_HAL::micros();
    rtt_dbg_copter_motors_calc_pwm_us = rtt_dbg_now_us - rtt_dbg_stage_us;
    rtt_dbg_copter_motors_calc_pwm_accum_us += rtt_dbg_copter_motors_calc_pwm_us;
    if (rtt_dbg_copter_motors_calc_pwm_us > 1000U) {
        rtt_dbg_copter_motors_calc_pwm_slow_count++;
    }
    rtt_dbg_update_max(rtt_dbg_copter_motors_calc_pwm_max_us, rtt_dbg_copter_motors_calc_pwm_us);
    rtt_dbg_stage_us = rtt_dbg_now_us;
#endif

    auto &srv = AP::srv();

    // cork now, so that all channel outputs happen at once
    srv.cork();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    rtt_dbg_now_us = AP_HAL::micros();
    rtt_dbg_copter_motors_cork_us = rtt_dbg_now_us - rtt_dbg_stage_us;
    rtt_dbg_copter_motors_cork_accum_us += rtt_dbg_copter_motors_cork_us;
    rtt_dbg_update_max(rtt_dbg_copter_motors_cork_max_us, rtt_dbg_copter_motors_cork_us);
    rtt_dbg_stage_us = rtt_dbg_now_us;
#endif

    // update output on any aux channels, for manual passthru
    SRV_Channels::output_ch_all();
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    rtt_dbg_now_us = AP_HAL::micros();
    rtt_dbg_copter_motors_output_ch_us = rtt_dbg_now_us - rtt_dbg_stage_us;
    rtt_dbg_copter_motors_output_ch_accum_us += rtt_dbg_copter_motors_output_ch_us;
    if (rtt_dbg_copter_motors_output_ch_us > 1000U) {
        rtt_dbg_copter_motors_output_ch_slow_count++;
    }
    rtt_dbg_update_max(rtt_dbg_copter_motors_output_ch_max_us, rtt_dbg_copter_motors_output_ch_us);
    rtt_dbg_stage_us = rtt_dbg_now_us;
#endif

    // update motors interlock state
    bool interlock = motors->armed() && !ap.in_arming_delay && (!ap.using_interlock || ap.motor_interlock_switch) && !SRV_Channels::get_emergency_stop();
    if (!motors->get_interlock() && interlock) {
        motors->set_interlock(true);
        LOGGER_WRITE_EVENT(LogEvent::MOTORS_INTERLOCK_ENABLED);
    } else if (motors->get_interlock() && !interlock) {
        motors->set_interlock(false);
        LOGGER_WRITE_EVENT(LogEvent::MOTORS_INTERLOCK_DISABLED);
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    rtt_dbg_now_us = AP_HAL::micros();
    rtt_dbg_copter_motors_interlock_us = rtt_dbg_now_us - rtt_dbg_stage_us;
    rtt_dbg_copter_motors_interlock_accum_us += rtt_dbg_copter_motors_interlock_us;
    rtt_dbg_update_max(rtt_dbg_copter_motors_interlock_max_us, rtt_dbg_copter_motors_interlock_us);
    rtt_dbg_stage_us = rtt_dbg_now_us;
#endif

    if (ap.motor_test) {
        // check if we are performing the motor test
        motor_test_output();
    } else {
        // send output signals to motors
        flightmode->output_to_motors();
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    rtt_dbg_now_us = AP_HAL::micros();
    rtt_dbg_copter_motors_flightmode_us = rtt_dbg_now_us - rtt_dbg_stage_us;
    rtt_dbg_copter_motors_flightmode_accum_us += rtt_dbg_copter_motors_flightmode_us;
    if (rtt_dbg_copter_motors_flightmode_us > 1000U) {
        rtt_dbg_copter_motors_flightmode_slow_count++;
    }
    rtt_dbg_update_max(rtt_dbg_copter_motors_flightmode_max_us, rtt_dbg_copter_motors_flightmode_us);
    rtt_dbg_stage_us = rtt_dbg_now_us;
#endif

    // push all channels
    if (full_push) {
        // motor output including servos and other updates that need to run at the main loop rate
        srv.push();
    } else {
        // motor output only at main loop rate or faster
        hal.rcout->push();
    }
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    rtt_dbg_now_us = AP_HAL::micros();
    rtt_dbg_copter_motors_push_us = rtt_dbg_now_us - rtt_dbg_stage_us;
    rtt_dbg_copter_motors_push_accum_us += rtt_dbg_copter_motors_push_us;
    if (rtt_dbg_copter_motors_push_us > 1000U) {
        rtt_dbg_copter_motors_push_slow_count++;
    }
    rtt_dbg_update_max(rtt_dbg_copter_motors_push_max_us, rtt_dbg_copter_motors_push_us);
    rtt_dbg_copter_motors_total_us = rtt_dbg_now_us - rtt_dbg_start_us;
    rtt_dbg_copter_motors_total_accum_us += rtt_dbg_copter_motors_total_us;
    if (rtt_dbg_copter_motors_total_us > 1000U) {
        rtt_dbg_copter_motors_total_slow_count++;
    }
    rtt_dbg_update_max(rtt_dbg_copter_motors_total_max_us, rtt_dbg_copter_motors_total_us);
#endif
}

// motors_output from main thread at main loop rate
void Copter::motors_output_main()
{
#if CONFIG_HAL_BOARD == HAL_BOARD_RTT && HAL_RTT_LOOP_DIAG
    rtt_dbg_copter_motors_main_calls++;
#endif
    if (!using_rate_thread) {
        motors_output();
    }
}

// check for pilot stick input to trigger lost vehicle alarm
void Copter::lost_vehicle_check()
{
    static uint8_t soundalarm_counter;

    // disable if aux switch is setup to vehicle alarm as the two could interfere
    if (rc().find_channel_for_option(RC_Channel::AUX_FUNC::LOST_VEHICLE_SOUND)) {
        return;
    }

    // ensure throttle is down, motors not armed, pitch and roll rc at max. Note: rc1=roll rc2=pitch
    if (ap.throttle_zero && !motors->armed() && (channel_roll->get_control_in() > 4000) && (channel_pitch->get_control_in() > 4000)) {
        if (soundalarm_counter >= LOST_VEHICLE_DELAY) {
            if (AP_Notify::flags.vehicle_lost == false) {
                AP_Notify::flags.vehicle_lost = true;
                gcs().send_text(MAV_SEVERITY_NOTICE,"Locate Copter alarm");
            }
        } else {
            soundalarm_counter++;
        }
    } else {
        soundalarm_counter = 0;
        if (AP_Notify::flags.vehicle_lost == true) {
            AP_Notify::flags.vehicle_lost = false;
        }
    }
}
