#!/usr/bin/env python3
"""RTT CUAV v5 main-loop rate diagnostic gate.

This gate is aimed at the Mission Planner symptom:

    PreArm: Main loop slow (190Hz < 400Hz)

It combines three signals:

* MAVLink STATUSTEXT, SYS_STATUS and selected parameters;
* message-rate observations while the board is connected normally over USB CDC;
* a short OpenOCD halt/resume snapshot of RTT debug counters.

The script is read-only for vehicle parameters.  It does not lower
SCHED_LOOP_RATE and does not mask arming checks.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pymavlink import mavutil

from rtt_usb_port_select import MAVLINK_PORT, resolve_mavlink_port


DEFAULT_PORT = MAVLINK_PORT
DEFAULT_ELF = "build/rtt_deploy/cuav_v5/rt-thread.elf"
DEFAULT_OPENOCD_CFG = ("interface/stlink.cfg", "target/stm32f7x.cfg")

PARAMS = (
    "SCHED_LOOP_RATE",
    "SCHED_DEBUG",
    "INS_FAST_SAMPLE",
    "INS_GYRO_RATE",
    "INS_ENABLE_MASK",
    "INS_USE",
    "INS_USE2",
    "INS_USE3",
    "LOG_DISARMED",
    "LOG_BACKEND_TYPE",
)

RTT_SYMBOLS = (
    "rtt_dbg_main_loop_iterations",
    "rtt_dbg_loop_time_us",
    "rtt_dbg_loop_time_accum_us",
    "rtt_dbg_loop_time_max_us",
    "rtt_dbg_loop_time_min_us",
    "rtt_dbg_work_time_us",
    "rtt_dbg_work_time_accum_us",
    "rtt_dbg_work_time_max_us",
    "rtt_dbg_overrun_count",
    "rtt_dbg_wait_sample_us",
    "rtt_dbg_run_tasks_us",
    "rtt_dbg_extra_loop",
    "rtt_dbg_fast_loop_count",
    "rtt_dbg_monitor_loop_delay_ms",
    "rtt_dbg_monitor_loop_delay_max_ms",
    "rtt_dbg_monitor_stuck_count",
    "rtt_dbg_monitor_stuck_task",
    "rtt_dbg_monitor_stuck_semline",
    "rtt_dbg_boost_calls_per_loop",
    "rtt_dbg_boost_total_us_per_loop",
    "rtt_dbg_boost_calls_total",
    "rtt_dbg_boost_requested_us_total",
    "rtt_dbg_boost_elapsed_us_total",
    "rtt_dbg_boost_yield_count_total",
    "rtt_dbg_boost_last_requested_us",
    "rtt_dbg_boost_last_elapsed_us",
    "rtt_dbg_boost_last_yield_elapsed_us",
    "rtt_dbg_boost_hrtimer_count",
    "rtt_dbg_boost_hrtimer_fail_count",
    "rtt_dbg_boost_hrtimer_last_elapsed_us",
    "rtt_dbg_clock_time_init_count",
    "rtt_dbg_clock_time_last_delta_us",
    "rtt_dbg_clock_time_irq_count",
    "rtt_dbg_clock_time_timeout_count",
    "rtt_dbg_ins_stage",
    "rtt_dbg_ins_loop_rate",
    "rtt_dbg_ins_backend_count",
    "rtt_dbg_ins_gyro_count",
    "rtt_dbg_ins_accel_count",
    "rtt_dbg_ins_wait_calls",
    "rtt_dbg_ins_wait_counter",
    "rtt_dbg_ins_wait_limit",
    "rtt_dbg_ins_gyro_avail_mask",
    "rtt_dbg_ins_accel_avail_mask",
    "rtt_dbg_ins_gyro_wait_mask",
    "rtt_dbg_ins_accel_wait_mask",
    "rtt_dbg_ins_new_gyro_mask",
    "rtt_dbg_ins_new_accel_mask",
    "rtt_dbg_ins_schedule_wait_accum_us",
    "rtt_dbg_ins_data_wait_accum_us",
    "rtt_dbg_ins_data_wait_loops",
    "rtt_dbg_ins_wait_limit_breaks",
    "rtt_dbg_scheduler_wait_sample_last_us",
    "rtt_dbg_scheduler_wait_sample_accum_us",
    "rtt_dbg_scheduler_wait_sample_max_us",
    "rtt_dbg_scheduler_wait_sample_large_count",
    "rtt_dbg_scheduler_run_time_available_last_us",
    "rtt_dbg_scheduler_run_time_available_min_us",
    "rtt_dbg_scheduler_run_total_us",
    "rtt_dbg_scheduler_run_accum_us",
    "rtt_dbg_scheduler_run_max_us",
    "rtt_dbg_scheduler_task_last_idx",
    "rtt_dbg_scheduler_task_last_us",
    "rtt_dbg_scheduler_task_last_allowed_us",
    "rtt_dbg_scheduler_task_max_idx",
    "rtt_dbg_scheduler_task_max_us",
    "rtt_dbg_scheduler_task_max_allowed_us",
    "rtt_dbg_scheduler_task_overrun_count",
    "rtt_dbg_scheduler_task_not_achieved_count",
    "rtt_dbg_scheduler_extra_loop_us",
    "rtt_dbg_scheduler_task_run_counts",
    "rtt_dbg_scheduler_task_last_us_by_idx",
    "rtt_dbg_scheduler_task_total_us_by_idx",
    "rtt_dbg_scheduler_task_max_us_by_idx",
    "rtt_dbg_scheduler_task_overrun_counts",
    "rtt_dbg_scheduler_task_not_achieved_counts",
    "rtt_dbg_scheduler_task_skip_budget_counts",
    "rtt_dbg_scheduler_task_priority_by_idx",
    "rtt_dbg_scheduler_task_rate_mhz_by_idx",
    "rtt_dbg_scheduler_task_allowed_us_by_idx",
    "rtt_dbg_scheduler_task_source_by_idx",
    "rtt_dbg_scheduler_task_name0_by_idx",
    "rtt_dbg_scheduler_task_name1_by_idx",
    "rtt_dbg_scheduler_task_name2_by_idx",
    "rtt_dbg_scheduler_task_name3_by_idx",
    "rtt_dbg_scheduler_task_name4_by_idx",
    "rtt_dbg_scheduler_task_name5_by_idx",
    "rtt_dbg_scheduler_task_last_dt_by_idx",
    "rtt_dbg_scheduler_task_interval_by_idx",
    "rtt_dbg_scheduler_task_skip_time_available_by_idx",
    "rtt_dbg_gcs_global_update_send_total_us",
    "rtt_dbg_gcs_global_update_send_total_max_us",
    "rtt_dbg_gcs_update_send_total_us",
    "rtt_dbg_gcs_update_send_total_max_us",
    "rtt_dbg_gcs_update_send_log_us",
    "rtt_dbg_gcs_update_send_log_max_us",
    "rtt_dbg_gcs_update_send_check_tasks_us",
    "rtt_dbg_gcs_update_send_check_tasks_max_us",
    "rtt_dbg_gcs_update_send_loop_us",
    "rtt_dbg_gcs_update_send_loop_max_us",
    "rtt_dbg_gcs_update_send_loop_iters",
    "rtt_dbg_gcs_update_send_loop_iters_max",
    "rtt_dbg_gcs_service_statustext_us",
    "rtt_dbg_gcs_service_statustext_max_us",
    "rtt_dbg_gcs_service_statustext_sent",
    "rtt_dbg_gcs_service_statustext_sent_max",
    "rtt_dbg_gcs_service_statustext_budget_breaks",
    "rtt_dbg_gcs_update_send_break_out_of_time",
    "rtt_dbg_gcs_update_send_break_try_send",
    "rtt_dbg_gcs_update_send_break_try_send_id",
    "rtt_dbg_gcs_update_send_last_txspace",
    "rtt_dbg_log_send_batch_us",
    "rtt_dbg_log_send_batch_max_us",
    "rtt_dbg_log_send_data_us",
    "rtt_dbg_log_send_data_max_us",
    "rtt_dbg_log_send_batch_sent",
    "rtt_dbg_log_send_batch_sent_max",
    "rtt_dbg_log_send_batch_budget_breaks",
    "rtt_dbg_log_send_batch_no_space_breaks",
    "rtt_dbg_log_send_num_sends",
    "rtt_dbg_slcan_report_calls",
    "rtt_dbg_slcan_report_sent",
    "rtt_dbg_slcan_report_no_space",
    "rtt_dbg_slcan_report_low_space_drops",
    "rtt_dbg_slcan_report_us",
    "rtt_dbg_slcan_report_max_us",
    "rtt_dbg_ahrs_update_calls",
    "rtt_dbg_ahrs_update_total_accum_us",
    "rtt_dbg_ahrs_update_total_max_us",
    "rtt_dbg_ahrs_orientation_accum_us",
    "rtt_dbg_ahrs_dcm_accum_us",
    "rtt_dbg_ahrs_dcm_skip_count",
    "rtt_dbg_ahrs_dcm_update_count",
    "rtt_dbg_ahrs_dcm_condition_count",
    "rtt_dbg_ahrs_ekf2_accum_us",
    "rtt_dbg_ahrs_ekf3_accum_us",
    "rtt_dbg_ahrs_view_accum_us",
    "rtt_dbg_ahrs_aoa_ssa_accum_us",
    "rtt_dbg_ahrs_update_state_accum_us",
    "rtt_dbg_copter_motors_total_us",
    "rtt_dbg_copter_motors_total_accum_us",
    "rtt_dbg_copter_motors_total_max_us",
    "rtt_dbg_copter_motors_total_slow_count",
    "rtt_dbg_copter_motors_calc_pwm_us",
    "rtt_dbg_copter_motors_calc_pwm_accum_us",
    "rtt_dbg_copter_motors_calc_pwm_max_us",
    "rtt_dbg_copter_motors_calc_pwm_slow_count",
    "rtt_dbg_copter_motors_cork_us",
    "rtt_dbg_copter_motors_cork_accum_us",
    "rtt_dbg_copter_motors_cork_max_us",
    "rtt_dbg_copter_motors_output_ch_us",
    "rtt_dbg_copter_motors_output_ch_accum_us",
    "rtt_dbg_copter_motors_output_ch_max_us",
    "rtt_dbg_copter_motors_output_ch_slow_count",
    "rtt_dbg_copter_motors_interlock_us",
    "rtt_dbg_copter_motors_interlock_accum_us",
    "rtt_dbg_copter_motors_interlock_max_us",
    "rtt_dbg_copter_motors_flightmode_us",
    "rtt_dbg_copter_motors_flightmode_accum_us",
    "rtt_dbg_copter_motors_flightmode_max_us",
    "rtt_dbg_copter_motors_flightmode_slow_count",
    "rtt_dbg_copter_motors_push_us",
    "rtt_dbg_copter_motors_push_accum_us",
    "rtt_dbg_copter_motors_push_max_us",
    "rtt_dbg_copter_motors_push_slow_count",
    "rtt_dbg_copter_motors_main_calls",
    "rtt_dbg_copter_read_ahrs_us",
    "rtt_dbg_copter_read_ahrs_accum_us",
    "rtt_dbg_copter_read_ahrs_max_us",
    "rtt_dbg_copter_read_ahrs_slow_count",
    "rtt_dbg_copter_read_ahrs_calls",
    "rtt_dbg_rcout_push_calls",
    "rtt_dbg_rcout_push_local_us",
    "rtt_dbg_rcout_push_local_accum_us",
    "rtt_dbg_rcout_push_local_max_us",
    "rtt_dbg_rcout_push_iomcu_us",
    "rtt_dbg_rcout_push_iomcu_accum_us",
    "rtt_dbg_rcout_push_iomcu_max_us",
    "rtt_dbg_rcout_push_total_us",
    "rtt_dbg_rcout_push_total_accum_us",
    "rtt_dbg_rcout_push_total_max_us",
    "rtt_dbg_uart_wait_timeout_calls",
    "rtt_dbg_uart_wait_timeout_iomcu_calls",
    "rtt_dbg_uart_wait_timeout_iomcu_us",
    "rtt_dbg_uart_wait_timeout_iomcu_accum_us",
    "rtt_dbg_uart_wait_timeout_iomcu_max_us",
    "rtt_dbg_uart_wait_timeout_iomcu_yields",
    "rtt_dbg_uart_wait_timeout_iomcu_spin_loops",
    "rtt_dbg_iomcu_thread_loops",
    "rtt_dbg_iomcu_thread_loop_us",
    "rtt_dbg_iomcu_thread_loop_accum_us",
    "rtt_dbg_iomcu_thread_loop_max_us",
    "rtt_dbg_iomcu_wait_event_us",
    "rtt_dbg_iomcu_wait_event_accum_us",
    "rtt_dbg_iomcu_send_servo_us",
    "rtt_dbg_iomcu_send_servo_accum_us",
    "rtt_dbg_iomcu_send_servo_calls",
    "rtt_dbg_iomcu_send_servo_event_calls",
    "rtt_dbg_iomcu_periodic_us",
    "rtt_dbg_iomcu_periodic_accum_us",
    "rtt_dbg_iomcu_write_registers_calls",
    "rtt_dbg_iomcu_write_registers_accum_us",
    "rtt_dbg_iomcu_write_registers_max_us",
    "rtt_dbg_iomcu_read_registers_calls",
    "rtt_dbg_iomcu_read_registers_accum_us",
    "rtt_dbg_iomcu_read_registers_max_us",
    "rtt_dbg_iomcu_push_calls",
    "rtt_dbg_iomcu_push_dirty_events",
    "rtt_dbg_iomcu_push_clean_skips",
    "rtt_dbg_thread_hook_calls",
    "rtt_dbg_thread_hook_overflow",
    "rtt_dbg_thread_switch_counts",
    "rtt_dbg_thread_run_total_us_by_idx",
    "rtt_dbg_thread_run_last_us_by_idx",
    "rtt_dbg_thread_run_max_us_by_idx",
    "rtt_dbg_thread_priority_by_idx",
    "rtt_dbg_thread_name0_by_idx",
    "rtt_dbg_thread_name1_by_idx",
    "rtt_dbg_thread_name2_by_idx",
    "rtt_dbg_thread_name3_by_idx",
    "rtt_cpu_idle_pct",
)

RTT_OPTIONAL_SYMBOLS = (
    "rtt_dbg_run_tasks_us",
    "rtt_dbg_extra_loop",
    "rtt_dbg_scheduler_task_run_counts",
    "rtt_dbg_scheduler_task_last_us_by_idx",
    "rtt_dbg_scheduler_task_total_us_by_idx",
    "rtt_dbg_scheduler_task_max_us_by_idx",
    "rtt_dbg_scheduler_task_overrun_counts",
    "rtt_dbg_scheduler_task_not_achieved_counts",
    "rtt_dbg_scheduler_task_skip_budget_counts",
    "rtt_dbg_scheduler_task_last_dt_by_idx",
    "rtt_dbg_scheduler_task_interval_by_idx",
    "rtt_dbg_scheduler_task_skip_time_available_by_idx",
    "rtt_dbg_copter_motors_total_us",
    "rtt_dbg_copter_motors_total_accum_us",
    "rtt_dbg_copter_motors_total_max_us",
    "rtt_dbg_copter_motors_total_slow_count",
    "rtt_dbg_copter_motors_calc_pwm_us",
    "rtt_dbg_copter_motors_calc_pwm_accum_us",
    "rtt_dbg_copter_motors_calc_pwm_max_us",
    "rtt_dbg_copter_motors_calc_pwm_slow_count",
    "rtt_dbg_copter_motors_cork_us",
    "rtt_dbg_copter_motors_cork_accum_us",
    "rtt_dbg_copter_motors_cork_max_us",
    "rtt_dbg_copter_motors_output_ch_us",
    "rtt_dbg_copter_motors_output_ch_accum_us",
    "rtt_dbg_copter_motors_output_ch_max_us",
    "rtt_dbg_copter_motors_output_ch_slow_count",
    "rtt_dbg_copter_motors_interlock_us",
    "rtt_dbg_copter_motors_interlock_accum_us",
    "rtt_dbg_copter_motors_interlock_max_us",
    "rtt_dbg_copter_motors_flightmode_us",
    "rtt_dbg_copter_motors_flightmode_accum_us",
    "rtt_dbg_copter_motors_flightmode_max_us",
    "rtt_dbg_copter_motors_flightmode_slow_count",
    "rtt_dbg_copter_motors_push_us",
    "rtt_dbg_copter_motors_push_accum_us",
    "rtt_dbg_copter_motors_push_max_us",
    "rtt_dbg_copter_motors_push_slow_count",
    "rtt_dbg_copter_motors_main_calls",
    "rtt_dbg_thread_hook_calls",
    "rtt_dbg_thread_hook_overflow",
    "rtt_dbg_thread_switch_counts",
    "rtt_dbg_thread_run_total_us_by_idx",
    "rtt_dbg_thread_run_last_us_by_idx",
    "rtt_dbg_thread_run_max_us_by_idx",
    "rtt_dbg_thread_priority_by_idx",
    "rtt_dbg_thread_name0_by_idx",
    "rtt_dbg_thread_name1_by_idx",
    "rtt_dbg_thread_name2_by_idx",
    "rtt_dbg_thread_name3_by_idx",
)

RTT_REQUIRED_SYMBOLS = tuple(
    name for name in RTT_SYMBOLS
    if name not in set(RTT_OPTIONAL_SYMBOLS)
)

RTT_TASK_SLOTS = 128
RTT_THREAD_SLOTS = 32
RTT_TASK_ARRAY_SYMBOLS = (
    "rtt_dbg_scheduler_task_run_counts",
    "rtt_dbg_scheduler_task_last_us_by_idx",
    "rtt_dbg_scheduler_task_total_us_by_idx",
    "rtt_dbg_scheduler_task_max_us_by_idx",
    "rtt_dbg_scheduler_task_overrun_counts",
    "rtt_dbg_scheduler_task_not_achieved_counts",
    "rtt_dbg_scheduler_task_skip_budget_counts",
    "rtt_dbg_scheduler_task_priority_by_idx",
    "rtt_dbg_scheduler_task_rate_mhz_by_idx",
    "rtt_dbg_scheduler_task_allowed_us_by_idx",
    "rtt_dbg_scheduler_task_source_by_idx",
    "rtt_dbg_scheduler_task_name0_by_idx",
    "rtt_dbg_scheduler_task_name1_by_idx",
    "rtt_dbg_scheduler_task_name2_by_idx",
    "rtt_dbg_scheduler_task_name3_by_idx",
    "rtt_dbg_scheduler_task_name4_by_idx",
    "rtt_dbg_scheduler_task_name5_by_idx",
    "rtt_dbg_scheduler_task_last_dt_by_idx",
    "rtt_dbg_scheduler_task_interval_by_idx",
    "rtt_dbg_scheduler_task_skip_time_available_by_idx",
)
RTT_THREAD_ARRAY_SYMBOLS = (
    "rtt_dbg_thread_switch_counts",
    "rtt_dbg_thread_run_total_us_by_idx",
    "rtt_dbg_thread_run_last_us_by_idx",
    "rtt_dbg_thread_run_max_us_by_idx",
    "rtt_dbg_thread_priority_by_idx",
    "rtt_dbg_thread_name0_by_idx",
    "rtt_dbg_thread_name1_by_idx",
    "rtt_dbg_thread_name2_by_idx",
    "rtt_dbg_thread_name3_by_idx",
)

RTT_SCALAR_DELTA_SYMBOLS = (
    "rtt_dbg_main_loop_iterations",
    "rtt_dbg_loop_time_accum_us",
    "rtt_dbg_work_time_accum_us",
    "rtt_dbg_overrun_count",
    "rtt_dbg_loop_time_max_us",
    "rtt_dbg_work_time_max_us",
    "rtt_dbg_fast_loop_count",
    "rtt_dbg_ins_wait_calls",
    "rtt_dbg_ins_wait_counter",
    "rtt_dbg_ins_schedule_wait_accum_us",
    "rtt_dbg_ins_data_wait_accum_us",
    "rtt_dbg_ins_data_wait_loops",
    "rtt_dbg_ins_wait_limit_breaks",
    "rtt_dbg_scheduler_wait_sample_large_count",
    "rtt_dbg_scheduler_wait_sample_accum_us",
    "rtt_dbg_scheduler_run_total_us",
    "rtt_dbg_scheduler_run_accum_us",
    "rtt_dbg_scheduler_run_max_us",
    "rtt_dbg_scheduler_task_overrun_count",
    "rtt_dbg_scheduler_task_not_achieved_count",
    "rtt_dbg_scheduler_extra_loop_us",
    "rtt_dbg_ahrs_update_calls",
    "rtt_dbg_ahrs_update_total_accum_us",
    "rtt_dbg_ahrs_update_total_max_us",
    "rtt_dbg_ahrs_orientation_accum_us",
    "rtt_dbg_ahrs_dcm_accum_us",
    "rtt_dbg_ahrs_dcm_skip_count",
    "rtt_dbg_ahrs_dcm_update_count",
    "rtt_dbg_ahrs_dcm_condition_count",
    "rtt_dbg_ahrs_ekf2_accum_us",
    "rtt_dbg_ahrs_ekf3_accum_us",
    "rtt_dbg_ahrs_view_accum_us",
    "rtt_dbg_ahrs_aoa_ssa_accum_us",
    "rtt_dbg_ahrs_update_state_accum_us",
    "rtt_dbg_copter_motors_total_accum_us",
    "rtt_dbg_copter_motors_total_slow_count",
    "rtt_dbg_copter_motors_calc_pwm_accum_us",
    "rtt_dbg_copter_motors_calc_pwm_slow_count",
    "rtt_dbg_copter_motors_cork_accum_us",
    "rtt_dbg_copter_motors_output_ch_accum_us",
    "rtt_dbg_copter_motors_output_ch_slow_count",
    "rtt_dbg_copter_motors_interlock_accum_us",
    "rtt_dbg_copter_motors_flightmode_accum_us",
    "rtt_dbg_copter_motors_flightmode_slow_count",
    "rtt_dbg_copter_motors_push_accum_us",
    "rtt_dbg_copter_motors_push_slow_count",
    "rtt_dbg_copter_motors_main_calls",
    "rtt_dbg_copter_read_ahrs_accum_us",
    "rtt_dbg_copter_read_ahrs_slow_count",
    "rtt_dbg_copter_read_ahrs_calls",
    "rtt_dbg_rcout_push_calls",
    "rtt_dbg_rcout_push_local_accum_us",
    "rtt_dbg_rcout_push_iomcu_accum_us",
    "rtt_dbg_rcout_push_total_accum_us",
    "rtt_dbg_uart_wait_timeout_calls",
    "rtt_dbg_uart_wait_timeout_iomcu_calls",
    "rtt_dbg_uart_wait_timeout_iomcu_accum_us",
    "rtt_dbg_uart_wait_timeout_iomcu_yields",
    "rtt_dbg_uart_wait_timeout_iomcu_spin_loops",
    "rtt_dbg_iomcu_thread_loops",
    "rtt_dbg_iomcu_thread_loop_accum_us",
    "rtt_dbg_iomcu_wait_event_accum_us",
    "rtt_dbg_iomcu_send_servo_accum_us",
    "rtt_dbg_iomcu_send_servo_calls",
    "rtt_dbg_iomcu_send_servo_event_calls",
    "rtt_dbg_iomcu_periodic_accum_us",
    "rtt_dbg_iomcu_write_registers_calls",
    "rtt_dbg_iomcu_write_registers_accum_us",
    "rtt_dbg_iomcu_read_registers_calls",
    "rtt_dbg_iomcu_read_registers_accum_us",
    "rtt_dbg_iomcu_push_calls",
    "rtt_dbg_iomcu_push_dirty_events",
    "rtt_dbg_iomcu_push_clean_skips",
    "rtt_dbg_boost_calls_total",
    "rtt_dbg_boost_requested_us_total",
    "rtt_dbg_boost_elapsed_us_total",
    "rtt_dbg_boost_yield_count_total",
    "rtt_dbg_gcs_global_update_send_total_us",
    "rtt_dbg_gcs_global_update_send_total_max_us",
    "rtt_dbg_gcs_update_send_total_us",
    "rtt_dbg_gcs_update_send_total_max_us",
    "rtt_dbg_gcs_update_send_log_us",
    "rtt_dbg_gcs_update_send_log_max_us",
    "rtt_dbg_gcs_update_send_check_tasks_us",
    "rtt_dbg_gcs_update_send_check_tasks_max_us",
    "rtt_dbg_gcs_update_send_loop_us",
    "rtt_dbg_gcs_update_send_loop_max_us",
    "rtt_dbg_gcs_update_send_loop_iters",
    "rtt_dbg_gcs_update_send_loop_iters_max",
    "rtt_dbg_gcs_service_statustext_us",
    "rtt_dbg_gcs_service_statustext_max_us",
    "rtt_dbg_gcs_service_statustext_sent",
    "rtt_dbg_gcs_service_statustext_sent_max",
    "rtt_dbg_gcs_service_statustext_budget_breaks",
    "rtt_dbg_gcs_update_send_break_out_of_time",
    "rtt_dbg_gcs_update_send_break_try_send",
)

FAULT_REGS = {
    "CFSR": 0xE000ED28,
    "HFSR": 0xE000ED2C,
    "DFSR": 0xE000ED30,
    "AFSR": 0xE000ED3C,
    "VTOR": 0xE000ED08,
}


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve_port(port_arg: str) -> str:
    return resolve_mavlink_port(port_arg)


def close_conn(conn: Any | None) -> None:
    if conn is None:
        return
    try:
        conn.close()
    except Exception:
        pass


def drain(conn: Any, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if conn.recv_match(blocking=False) is None:
            time.sleep(0.01)


def message_text(msg: Any) -> str:
    text = getattr(msg, "text", "")
    if isinstance(text, bytes):
        return text.decode(errors="ignore").rstrip("\x00")
    return str(text).rstrip("\x00")


def request_message_interval(conn: Any, msg_id: int, hz: float) -> None:
    interval_us = -1 if hz <= 0 else int(1_000_000 / hz)
    conn.mav.command_long_send(
        conn.target_system,
        conn.target_component,
        mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,
        0,
        msg_id,
        interval_us,
        0,
        0,
        0,
        0,
        0,
    )


def request_params(conn: Any, names: tuple[str, ...], timeout_s: float) -> dict[str, Any]:
    wanted = set(names)
    params: dict[str, Any] = {}
    for name in names:
        conn.mav.param_request_read_send(
            conn.target_system,
            conn.target_component,
            name.encode("ascii"),
            -1,
        )

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline and wanted:
        msg = conn.recv_match(type=["PARAM_VALUE", "STATUSTEXT"], blocking=True, timeout=0.5)
        if msg is None:
            continue
        if msg.get_type() != "PARAM_VALUE":
            continue
        pname = getattr(msg, "param_id", "")
        if isinstance(pname, bytes):
            pname = pname.decode(errors="ignore")
        pname = str(pname).rstrip("\x00")
        if pname in wanted:
            params[pname] = {
                "value": float(msg.param_value),
                "type": int(msg.param_type),
                "index": int(msg.param_index),
                "count": int(msg.param_count),
            }
            wanted.remove(pname)
    return params


def sample_mavlink(args: argparse.Namespace) -> dict[str, Any]:
    port = resolve_port(args.port)
    payload: dict[str, Any] = {
        "port": port,
        "sample_s": args.sample_s,
        "messages": {},
        "statustext": [],
        "sys_status": [],
        "heartbeat": [],
    }
    conn = mavutil.mavlink_connection(
        port,
        baud=args.baud,
        robust_parsing=True,
        source_system=args.source_system,
    )
    try:
        hb = conn.wait_heartbeat(timeout=args.heartbeat_timeout)
        if hb is None or conn.target_system == 0:
            payload.update({"connected": False, "reason": "no_heartbeat"})
            return payload
        payload["connected"] = True
        payload["target_system"] = int(conn.target_system)
        payload["target_component"] = int(conn.target_component)
        payload["first_heartbeat"] = hb.to_dict()

        if args.skip_param_requests:
            payload["params"] = {}
        else:
            payload["params"] = request_params(conn, PARAMS, args.param_timeout)

        if not args.skip_message_interval_requests:
            request_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_SYS_STATUS, 4.0)
            request_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_RAW_IMU, 50.0)
            request_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_ATTITUDE, 20.0)
            request_message_interval(conn, mavutil.mavlink.MAVLINK_MSG_ID_SCALED_PRESSURE, 20.0)
        drain(conn, args.drain_s)

        start = time.monotonic()
        last_times: dict[str, float] = {}
        gaps: dict[str, list[float]] = {}
        counts: dict[str, int] = {}
        statustext: list[dict[str, Any]] = []
        sys_status: list[dict[str, Any]] = []
        heartbeat: list[dict[str, Any]] = []
        raw_imu_nonzero = False
        imu_banner: list[str] = []
        main_loop_slow: list[str] = []

        while time.monotonic() - start < args.sample_s:
            msg = conn.recv_match(blocking=True, timeout=1.0)
            if msg is None:
                continue
            mtype = msg.get_type()
            now = time.monotonic()
            counts[mtype] = counts.get(mtype, 0) + 1
            if mtype in last_times:
                gaps.setdefault(mtype, []).append(now - last_times[mtype])
            last_times[mtype] = now
            if mtype == "STATUSTEXT":
                text = message_text(msg)
                item = {
                    "t_s": round(now - start, 3),
                    "severity": int(getattr(msg, "severity", -1)),
                    "text": text,
                }
                statustext.append(item)
                if "fast sampling enabled" in text:
                    imu_banner.append(text)
                if "Main loop slow" in text:
                    main_loop_slow.append(text)
            elif mtype == "SYS_STATUS":
                sys_status.append({
                    "t_s": round(now - start, 3),
                    "load": int(getattr(msg, "load", 0)),
                    "onboard_control_sensors_present": int(getattr(msg, "onboard_control_sensors_present", 0)),
                    "onboard_control_sensors_enabled": int(getattr(msg, "onboard_control_sensors_enabled", 0)),
                    "onboard_control_sensors_health": int(getattr(msg, "onboard_control_sensors_health", 0)),
                })
            elif mtype == "HEARTBEAT":
                heartbeat.append({
                    "t_s": round(now - start, 3),
                    "system_status": int(getattr(msg, "system_status", -1)),
                })
            elif mtype == "RAW_IMU":
                raw_imu_nonzero = raw_imu_nonzero or any(
                    int(getattr(msg, field, 0)) != 0
                    for field in ("xacc", "yacc", "zacc", "xgyro", "ygyro", "zgyro", "xmag", "ymag", "zmag")
                )

        rates: dict[str, Any] = {}
        for mtype, count in counts.items():
            glist = gaps.get(mtype, [])
            rates[mtype] = {
                "count": count,
                "rate_hz": round(count / max(args.sample_s, 0.001), 2),
                "max_gap_s": round(max(glist), 3) if glist else None,
            }
        payload.update({
            "messages": rates,
            "statustext": statustext[-80:],
            "main_loop_slow_text": main_loop_slow,
            "imu_banner_text": imu_banner,
            "sys_status": sys_status[-20:],
            "heartbeat": heartbeat[-20:],
            "raw_imu_nonzero": raw_imu_nonzero,
        })
        return payload
    finally:
        close_conn(conn)


def cleanup_openocd() -> None:
    subprocess.run(["pkill", "-9", "-x", "openocd"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "[o]penoccd"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def symbol_addresses(elf: Path, nm: str) -> dict[str, int]:
    proc = subprocess.run([nm, "-g", str(elf)], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    wanted = set(RTT_SYMBOLS)
    addrs: dict[str, int] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        name = parts[-1]
        if name in wanted:
            addrs[name] = int(parts[0], 16)
    return addrs


def parse_openocd(output: str, addrs: dict[str, int]) -> tuple[dict[str, int], dict[str, int], dict[str, int | None], dict[str, list[int]]]:
    addr_to_symbol = {addr: name for name, addr in addrs.items()}
    addr_to_fault = {addr: name for name, addr in FAULT_REGS.items()}
    array_ranges = {
        name: (addrs[name], addrs[name] + RTT_TASK_SLOTS * 4)
        for name in RTT_TASK_ARRAY_SYMBOLS
        if name in addrs
    }
    array_ranges.update({
        name: (addrs[name], addrs[name] + RTT_THREAD_SLOTS * 4)
        for name in RTT_THREAD_ARRAY_SYMBOLS
        if name in addrs
    })
    values: dict[str, int] = {}
    arrays: dict[str, list[int]] = {
        name: [0] * (RTT_THREAD_SLOTS if name in RTT_THREAD_ARRAY_SYMBOLS else RTT_TASK_SLOTS)
        for name in array_ranges
    }
    faults: dict[str, int] = {}
    registers: dict[str, int | None] = {"pc": None, "msp": None, "psp": None}
    mdw_re = re.compile(r"0x([0-9a-fA-F]{8}):((?:\s+[0-9a-fA-F]{8})+)")
    reg_re = re.compile(r"^(pc|msp|psp)\s+\(/32\):\s+0x([0-9a-fA-F]+)", re.MULTILINE)
    for match in mdw_re.finditer(output):
        addr = int(match.group(1), 16)
        words = [int(item, 16) for item in match.group(2).split()]
        for offset, value in enumerate(words):
            word_addr = addr + offset * 4
            if word_addr in addr_to_symbol and addr_to_symbol[word_addr] not in RTT_TASK_ARRAY_SYMBOLS:
                values[addr_to_symbol[word_addr]] = value
            if word_addr in addr_to_fault:
                faults[addr_to_fault[word_addr]] = value
            for name, (start, end) in array_ranges.items():
                if start <= word_addr < end:
                    arrays[name][(word_addr - start) // 4] = value
    for match in reg_re.finditer(output):
        registers[match.group(1)] = int(match.group(2), 16)
    return values, faults, registers, arrays


def unpack_name(words: list[int]) -> str:
    raw = bytearray()
    for word in words:
        for shift in (0, 8, 16, 24):
            ch = (int(word) >> shift) & 0xFF
            if ch == 0:
                return raw.decode("ascii", errors="replace")
            raw.append(ch)
    return raw.decode("ascii", errors="replace")


def task_meta(arrays: dict[str, list[int]], idx: int) -> dict[str, Any]:
    def arr_value(name: str) -> int:
        values = arrays.get(name, [])
        if 0 <= idx < len(values):
            return int(values[idx])
        return 0

    source = arr_value("rtt_dbg_scheduler_task_source_by_idx")
    return {
        "name": unpack_name([
            arr_value("rtt_dbg_scheduler_task_name0_by_idx"),
            arr_value("rtt_dbg_scheduler_task_name1_by_idx"),
            arr_value("rtt_dbg_scheduler_task_name2_by_idx"),
            arr_value("rtt_dbg_scheduler_task_name3_by_idx"),
            arr_value("rtt_dbg_scheduler_task_name4_by_idx"),
            arr_value("rtt_dbg_scheduler_task_name5_by_idx"),
        ]),
        "priority": arr_value("rtt_dbg_scheduler_task_priority_by_idx"),
        "rate_hz": round(arr_value("rtt_dbg_scheduler_task_rate_mhz_by_idx") / 1000.0, 3),
        "allowed_us": arr_value("rtt_dbg_scheduler_task_allowed_us_by_idx"),
        "source": {1: "vehicle", 2: "common"}.get(source, "unknown"),
        "last_dt_ticks": arr_value("rtt_dbg_scheduler_task_last_dt_by_idx"),
        "interval_ticks": arr_value("rtt_dbg_scheduler_task_interval_by_idx"),
        "skip_time_available_us": arr_value("rtt_dbg_scheduler_task_skip_time_available_by_idx"),
    }


def top_task_counters(arrays: dict[str, list[int]]) -> dict[str, list[dict[str, Any]]]:
    def top_for(name: str, count: int = 12) -> list[dict[str, int]]:
        values = arrays.get(name, [])
        items = [
            {"idx": idx, "value": int(value), **task_meta(arrays, idx)}
            for idx, value in enumerate(values)
            if value
        ]
        items.sort(key=lambda item: item["value"], reverse=True)
        return items[:count]

    return {
        "run_counts": top_for("rtt_dbg_scheduler_task_run_counts"),
        "last_us_by_idx": top_for("rtt_dbg_scheduler_task_last_us_by_idx"),
        "total_us_by_idx": top_for("rtt_dbg_scheduler_task_total_us_by_idx"),
        "max_us_by_idx": top_for("rtt_dbg_scheduler_task_max_us_by_idx"),
        "overrun_counts": top_for("rtt_dbg_scheduler_task_overrun_counts"),
        "not_achieved_counts": top_for("rtt_dbg_scheduler_task_not_achieved_counts"),
        "skip_budget_counts": top_for("rtt_dbg_scheduler_task_skip_budget_counts"),
    }


def thread_meta(arrays: dict[str, list[int]], idx: int) -> dict[str, Any]:
    def arr_value(name: str) -> int:
        values = arrays.get(name, [])
        if 0 <= idx < len(values):
            return int(values[idx])
        return 0

    return {
        "name": unpack_name([
            arr_value("rtt_dbg_thread_name0_by_idx"),
            arr_value("rtt_dbg_thread_name1_by_idx"),
            arr_value("rtt_dbg_thread_name2_by_idx"),
            arr_value("rtt_dbg_thread_name3_by_idx"),
        ]),
        "priority": arr_value("rtt_dbg_thread_priority_by_idx"),
    }


def top_thread_counters(arrays: dict[str, list[int]]) -> dict[str, list[dict[str, Any]]]:
    def top_for(name: str, count: int = 16) -> list[dict[str, Any]]:
        values = arrays.get(name, [])
        items = [
            {"idx": idx, "value": int(value), **thread_meta(arrays, idx)}
            for idx, value in enumerate(values)
            if value
        ]
        items.sort(key=lambda item: item["value"], reverse=True)
        return items[:count]

    return {
        "switch_counts": top_for("rtt_dbg_thread_switch_counts"),
        "run_total_us_by_idx": top_for("rtt_dbg_thread_run_total_us_by_idx"),
        "run_last_us_by_idx": top_for("rtt_dbg_thread_run_last_us_by_idx"),
        "run_max_us_by_idx": top_for("rtt_dbg_thread_run_max_us_by_idx"),
    }


def task_counter_deltas(before: dict[str, list[int]], after: dict[str, list[int]]) -> dict[str, list[dict[str, Any]]]:
    def delta_top(name: str, count: int = 12) -> list[dict[str, Any]]:
        before_values = before.get(name, [])
        after_values = after.get(name, [])
        size = max(len(before_values), len(after_values), RTT_TASK_SLOTS)
        items: list[dict[str, Any]] = []
        for idx in range(size):
            lhs = int(before_values[idx]) if idx < len(before_values) else 0
            rhs = int(after_values[idx]) if idx < len(after_values) else 0
            value = max(0, rhs - lhs)
            if value:
                items.append({"idx": idx, "value": value, **task_meta(after, idx)})
        items.sort(key=lambda item: item["value"], reverse=True)
        return items[:count]

    run_deltas: list[int] = []
    run_before = before.get("rtt_dbg_scheduler_task_run_counts", [])
    run_after = after.get("rtt_dbg_scheduler_task_run_counts", [])
    for idx in range(max(len(run_before), len(run_after), RTT_TASK_SLOTS)):
        lhs = int(run_before[idx]) if idx < len(run_before) else 0
        rhs = int(run_after[idx]) if idx < len(run_after) else 0
        run_deltas.append(max(0, rhs - lhs))

    total_before = before.get("rtt_dbg_scheduler_task_total_us_by_idx", [])
    total_after = after.get("rtt_dbg_scheduler_task_total_us_by_idx", [])
    total_items: list[dict[str, Any]] = []
    for idx in range(max(len(total_before), len(total_after), RTT_TASK_SLOTS)):
        lhs = int(total_before[idx]) if idx < len(total_before) else 0
        rhs = int(total_after[idx]) if idx < len(total_after) else 0
        value = max(0, rhs - lhs)
        if value:
            runs = run_deltas[idx] if idx < len(run_deltas) else 0
            item = {"idx": idx, "value": value, "runs": runs, "avg_us": round(value / runs, 2) if runs else None, **task_meta(after, idx)}
            total_items.append(item)
    total_items.sort(key=lambda item: item["value"], reverse=True)

    return {
        "run_counts": delta_top("rtt_dbg_scheduler_task_run_counts"),
        "total_us_by_idx": total_items[:12],
        "overrun_counts": delta_top("rtt_dbg_scheduler_task_overrun_counts"),
        "not_achieved_counts": delta_top("rtt_dbg_scheduler_task_not_achieved_counts"),
        "skip_budget_counts": delta_top("rtt_dbg_scheduler_task_skip_budget_counts"),
    }


def thread_counter_deltas(before: dict[str, list[int]], after: dict[str, list[int]]) -> dict[str, list[dict[str, Any]]]:
    def delta_top(name: str, count: int = 16) -> list[dict[str, Any]]:
        before_values = before.get(name, [])
        after_values = after.get(name, [])
        size = max(len(before_values), len(after_values), RTT_THREAD_SLOTS)
        items: list[dict[str, Any]] = []
        for idx in range(size):
            lhs = int(before_values[idx]) if idx < len(before_values) else 0
            rhs = int(after_values[idx]) if idx < len(after_values) else 0
            value = max(0, rhs - lhs)
            if value:
                items.append({"idx": idx, "value": value, **thread_meta(after, idx)})
        items.sort(key=lambda item: item["value"], reverse=True)
        return items[:count]

    switch_before = before.get("rtt_dbg_thread_switch_counts", [])
    switch_after = after.get("rtt_dbg_thread_switch_counts", [])
    switch_deltas: list[int] = []
    for idx in range(max(len(switch_before), len(switch_after), RTT_THREAD_SLOTS)):
        lhs = int(switch_before[idx]) if idx < len(switch_before) else 0
        rhs = int(switch_after[idx]) if idx < len(switch_after) else 0
        switch_deltas.append(max(0, rhs - lhs))

    total_before = before.get("rtt_dbg_thread_run_total_us_by_idx", [])
    total_after = after.get("rtt_dbg_thread_run_total_us_by_idx", [])
    total_items: list[dict[str, Any]] = []
    for idx in range(max(len(total_before), len(total_after), RTT_THREAD_SLOTS)):
        lhs = int(total_before[idx]) if idx < len(total_before) else 0
        rhs = int(total_after[idx]) if idx < len(total_after) else 0
        value = max(0, rhs - lhs)
        if value:
            switches = switch_deltas[idx] if idx < len(switch_deltas) else 0
            total_items.append({
                "idx": idx,
                "value": value,
                "switches": switches,
                "avg_us": round(value / switches, 2) if switches else None,
                **thread_meta(after, idx),
            })
    total_items.sort(key=lambda item: item["value"], reverse=True)
    return {
        "switch_counts": delta_top("rtt_dbg_thread_switch_counts"),
        "run_total_us_by_idx": total_items[:16],
        "run_max_us_by_idx": delta_top("rtt_dbg_thread_run_max_us_by_idx"),
    }


def scalar_deltas(before: dict[str, Any], after: dict[str, Any]) -> dict[str, int]:
    before_values = before.get("values", {}) if isinstance(before, dict) else {}
    after_values = after.get("values", {}) if isinstance(after, dict) else {}
    out: dict[str, int] = {}
    if not isinstance(before_values, dict) or not isinstance(after_values, dict):
        return out
    for name in RTT_SCALAR_DELTA_SYMBOLS:
        lhs = int(before_values.get(name) or 0)
        rhs = int(after_values.get(name) or 0)
        out[name] = max(0, rhs - lhs)
    return out


def average_loop_rate(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    before_values = before.get("values", {}) if isinstance(before, dict) else {}
    after_values = after.get("values", {}) if isinstance(after, dict) else {}
    if not isinstance(before_values, dict) or not isinstance(after_values, dict):
        return {"verdict": "UNKNOWN", "reason": "missing_values"}
    before_iter = before_values.get("rtt_dbg_main_loop_iterations")
    after_iter = after_values.get("rtt_dbg_main_loop_iterations")
    before_t = before.get("finished_monotonic_s")
    after_t = after.get("started_monotonic_s")
    if before_iter is None or after_iter is None or before_t is None or after_t is None:
        return {"verdict": "UNKNOWN", "reason": "missing_iteration_or_time"}
    delta_s = float(after_t) - float(before_t)
    delta_iterations = int(after_iter) - int(before_iter)
    if delta_s <= 0 or delta_iterations < 0:
        return {
            "verdict": "UNKNOWN",
            "reason": "invalid_delta",
            "delta_s": round(delta_s, 6),
            "delta_iterations": delta_iterations,
        }
    return {
        "verdict": "GREEN",
        "reason": "average_loop_rate_measured",
        "delta_s": round(delta_s, 6),
        "delta_iterations": delta_iterations,
        "avg_loop_hz": delta_iterations / delta_s,
    }


def openocd_snapshot(args: argparse.Namespace) -> dict[str, Any]:
    started_monotonic_s = time.monotonic()
    elf = Path(args.elf)
    if not elf.exists():
        return {"verdict": "RED", "reason": "elf_not_found", "elf": str(elf)}
    try:
        addrs = symbol_addresses(elf, args.nm)
    except Exception as exc:  # noqa: BLE001
        return {"verdict": "RED", "reason": f"symbol_lookup_failed:{exc!r}", "elf": str(elf)}

    missing = sorted(set(RTT_REQUIRED_SYMBOLS) - set(addrs))
    optional_missing = sorted(set(RTT_OPTIONAL_SYMBOLS) - set(addrs))
    commands = [
        "init",
        "halt",
        "reg pc",
        "reg msp",
        "reg psp",
    ]
    for name in RTT_SYMBOLS:
        if name in addrs:
            if name in RTT_TASK_ARRAY_SYMBOLS:
                commands.append(f"mdw 0x{addrs[name]:08x} {RTT_TASK_SLOTS}")
            else:
                commands.append(f"mdw 0x{addrs[name]:08x} 1")
    for name, addr in FAULT_REGS.items():
        commands.append(f"mdw 0x{addr:08x} 1")
    commands.extend(["resume", "shutdown"])

    argv = ["timeout", str(args.openocd_timeout), "openocd"]
    for cfg in args.openocd_cfg:
        argv.extend(["-f", cfg])
    for cmd in commands:
        argv.extend(["-c", cmd])

    cleanup_openocd()
    try:
        proc = subprocess.run(
            argv,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.openocd_timeout + 5,
        )
    except Exception as exc:  # noqa: BLE001
        cleanup_openocd()
        return {"verdict": "RED", "reason": f"openocd_exception:{exc!r}", "argv": argv}
    finally:
        cleanup_openocd()

    values, faults, registers, arrays = parse_openocd(proc.stdout, addrs)
    finished_monotonic_s = time.monotonic()
    return {
        "verdict": "GREEN" if proc.returncode == 0 else "RED",
        "reason": "snapshot_ok" if proc.returncode == 0 else "openocd_failed",
        "rc": proc.returncode,
        "started_monotonic_s": started_monotonic_s,
        "finished_monotonic_s": finished_monotonic_s,
        "argv": argv,
        "missing_symbols": missing,
        "optional_missing_symbols": optional_missing,
        "addresses": {name: f"0x{addr:08x}" for name, addr in sorted(addrs.items())},
        "values": values,
        "task_arrays": arrays,
        "task_tops": top_task_counters(arrays),
        "thread_tops": top_thread_counters(arrays),
        "faults": faults,
        "registers": registers,
        "output_tail": proc.stdout[-4000:],
    }


def wait_for_openocd_steady(args: argparse.Namespace) -> dict[str, Any]:
    deadline = time.monotonic() + args.openocd_steady_timeout_s
    previous: dict[str, Any] | None = None
    attempts: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        snap = openocd_snapshot(args)
        values = snap.get("values", {}) if isinstance(snap, dict) else {}
        current_iter = int(values.get("rtt_dbg_main_loop_iterations") or 0)
        ins_loop_rate = int(values.get("rtt_dbg_ins_loop_rate") or 0)
        attempts.append({
            "verdict": snap.get("verdict"),
            "reason": snap.get("reason"),
            "main_loop_iterations": current_iter,
            "ins_loop_rate": ins_loop_rate,
        })
        if previous is not None:
            previous_values = previous.get("values", {}) if isinstance(previous, dict) else {}
            previous_iter = int(previous_values.get("rtt_dbg_main_loop_iterations") or 0)
            if (snap.get("verdict") == "GREEN" and previous.get("verdict") == "GREEN" and
                    current_iter > previous_iter and ins_loop_rate == args.target_loop_rate):
                snap["steady_wait_attempts"] = attempts[-8:]
                snap["steady_wait_reason"] = "main_loop_counter_incremented"
                return snap
        previous = snap
        time.sleep(args.openocd_steady_poll_s)

    fallback = previous if previous is not None else {"verdict": "RED", "reason": "steady_wait_no_snapshot"}
    fallback["steady_wait_attempts"] = attempts[-8:]
    fallback["steady_wait_reason"] = "timeout"
    return fallback


def classify(payload: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    mav = payload.get("mavlink", {})
    snap = payload.get("openocd_snapshot", {})
    values = snap.get("values", {}) if isinstance(snap, dict) else {}
    params = mav.get("params", {}) if isinstance(mav, dict) else {}
    sched_param = params.get("SCHED_LOOP_RATE", {}) if isinstance(params, dict) else {}
    target_hz = int(round(float(sched_param.get("value", args.target_loop_rate))))

    loop_us = int(values.get("rtt_dbg_loop_time_us") or 0)
    loop_hz = (1_000_000.0 / loop_us) if loop_us > 0 else None
    avg_loop_hz = None
    avg_loop_delta_iterations = None
    avg_loop_delta_s = None
    average = payload.get("openocd_average_loop_rate", {})
    if isinstance(average, dict):
        avg_value = average.get("avg_loop_hz")
        if avg_value is not None:
            avg_loop_hz = float(avg_value)
        avg_loop_delta_iterations = average.get("delta_iterations")
        avg_loop_delta_s = average.get("delta_s")
    wait_last = int(values.get("rtt_dbg_scheduler_wait_sample_last_us") or values.get("rtt_dbg_wait_sample_us") or 0)
    wait_max = int(values.get("rtt_dbg_scheduler_wait_sample_max_us") or 0)
    work_us = int(values.get("rtt_dbg_work_time_us") or 0)
    work_max = int(values.get("rtt_dbg_work_time_max_us") or 0)
    time_avail_last = int(values.get("rtt_dbg_scheduler_run_time_available_last_us") or 0)
    overrun_count = int(values.get("rtt_dbg_overrun_count") or 0)
    ins_loop_rate = int(values.get("rtt_dbg_ins_loop_rate") or 0)
    scheduler_run_us = int(values.get("rtt_dbg_scheduler_run_total_us") or values.get("rtt_dbg_run_tasks_us") or 0)
    scheduler_run_max_us = int(values.get("rtt_dbg_scheduler_run_max_us") or 0)
    task_max_idx = int(values.get("rtt_dbg_scheduler_task_max_idx") or 0)
    task_max_us = int(values.get("rtt_dbg_scheduler_task_max_us") or 0)
    task_max_allowed_us = int(values.get("rtt_dbg_scheduler_task_max_allowed_us") or 0)
    task_last_idx = int(values.get("rtt_dbg_scheduler_task_last_idx") or 0)
    task_last_us = int(values.get("rtt_dbg_scheduler_task_last_us") or 0)
    task_last_allowed_us = int(values.get("rtt_dbg_scheduler_task_last_allowed_us") or 0)
    task_overrun_count = int(values.get("rtt_dbg_scheduler_task_overrun_count") or 0)
    task_not_achieved_count = int(values.get("rtt_dbg_scheduler_task_not_achieved_count") or 0)
    extra_loop_us = int(values.get("rtt_dbg_scheduler_extra_loop_us") or values.get("rtt_dbg_extra_loop") or 0)
    gcs_total_us = int(values.get("rtt_dbg_gcs_update_send_total_us") or 0)
    gcs_total_max_us = int(values.get("rtt_dbg_gcs_update_send_total_max_us") or 0)
    gcs_global_total_us = int(values.get("rtt_dbg_gcs_global_update_send_total_us") or 0)
    gcs_global_total_max_us = int(values.get("rtt_dbg_gcs_global_update_send_total_max_us") or 0)
    gcs_log_us = int(values.get("rtt_dbg_gcs_update_send_log_us") or 0)
    gcs_log_max_us = int(values.get("rtt_dbg_gcs_update_send_log_max_us") or 0)
    gcs_check_tasks_us = int(values.get("rtt_dbg_gcs_update_send_check_tasks_us") or 0)
    gcs_check_tasks_max_us = int(values.get("rtt_dbg_gcs_update_send_check_tasks_max_us") or 0)
    gcs_loop_us = int(values.get("rtt_dbg_gcs_update_send_loop_us") or 0)
    gcs_loop_max_us = int(values.get("rtt_dbg_gcs_update_send_loop_max_us") or 0)
    cpu_idle = values.get("rtt_cpu_idle_pct")
    cpu_load = None
    if cpu_idle is not None:
        cpu_load = max(0, min(100, 100 - int(cpu_idle)))

    main_loop_slow_text = mav.get("main_loop_slow_text") or []
    imu_banner_text = mav.get("imu_banner_text") or []
    sys_status = mav.get("sys_status") or []
    sys_load_latest = sys_status[-1].get("load") if sys_status else None

    thresholds = {
        "target_loop_rate_hz": target_hz,
        "min_loop_rate_hz": args.min_loop_rate,
        "loop_period_us": int(1_000_000 / max(target_hz, 1)),
        "sample_wait_suspicious_us": args.sample_wait_suspicious_us,
    }

    symptoms: list[str] = []
    if avg_loop_hz is not None and avg_loop_hz < args.min_loop_rate:
        symptoms.append("average_loop_rate_below_threshold")
    elif loop_hz is not None and loop_hz < args.min_loop_rate:
        symptoms.append("debug_loop_rate_below_threshold")
    if main_loop_slow_text:
        symptoms.append("statustext_main_loop_slow")
    if any("0.0kHz/0.0kHz" in text for text in imu_banner_text):
        symptoms.append("imu_fast_sampling_banner_zero_rate")
    if wait_last >= args.sample_wait_suspicious_us or wait_max >= args.sample_wait_suspicious_us:
        symptoms.append("wait_for_sample_suspicious")
    if overrun_count > 0:
        symptoms.append("main_loop_work_overrun_count_nonzero")
    if task_overrun_count > 0:
        symptoms.append("scheduler_task_overrun_count_nonzero")
    if task_not_achieved_count > 0 or extra_loop_us > 0:
        symptoms.append("scheduler_extra_loop_budget_active")

    diagnosis = "UNKNOWN"
    if "wait_for_sample_suspicious" in symptoms and work_us < thresholds["loop_period_us"]:
        diagnosis = "INS_SAMPLE_WAIT_LIMITED"
    elif task_max_us > task_max_allowed_us and task_max_allowed_us > 0:
        diagnosis = "SCHEDULER_TASK_OVERRUN"
    elif avg_loop_hz is not None and avg_loop_hz < args.min_loop_rate:
        diagnosis = "AVERAGE_LOOP_RATE_LOW"
    elif loop_hz is not None and loop_hz < args.min_loop_rate and work_us >= thresholds["loop_period_us"]:
        diagnosis = "MAIN_THREAD_WORK_OVER_BUDGET"
    elif loop_hz is not None and loop_hz < args.min_loop_rate:
        diagnosis = "LOOP_RATE_LOW_NEEDS_MORE_COUNTERS"
    elif any("imu_fast_sampling_banner_zero_rate" == item for item in symptoms):
        diagnosis = "IMU_RATE_REPORTING_ANOMALY"
    else:
        diagnosis = "LOOP_RATE_OK_OR_NOT_REPRODUCED"

    if avg_loop_hz is not None:
        final_rate_ok = avg_loop_hz >= args.min_loop_rate
    else:
        final_rate_ok = loop_hz is not None and loop_hz >= args.min_loop_rate
    statustext_ok = not main_loop_slow_text
    imu_banner_ok = not any("0.0kHz/0.0kHz" in text for text in imu_banner_text)
    verdict = "GREEN" if final_rate_ok and statustext_ok and imu_banner_ok else "RED"
    return {
        "verdict": verdict,
        "reason": "loop_rate_ok" if verdict == "GREEN" else "loop_rate_or_imu_banner_not_ok",
        "diagnosis": diagnosis,
        "symptoms": symptoms,
        "metrics": {
            "target_loop_rate_hz": target_hz,
            "min_loop_rate_hz": args.min_loop_rate,
            "debug_loop_us": loop_us,
            "debug_loop_hz": None if loop_hz is None else round(loop_hz, 1),
            "avg_loop_hz": None if avg_loop_hz is None else round(avg_loop_hz, 2),
            "avg_loop_delta_iterations": avg_loop_delta_iterations,
            "avg_loop_delta_s": avg_loop_delta_s,
            "wait_sample_last_us": wait_last,
            "wait_sample_max_us": wait_max,
            "work_time_us": work_us,
            "work_time_max_us": work_max,
            "scheduler_run_total_us": scheduler_run_us,
            "scheduler_run_max_us": scheduler_run_max_us,
            "time_available_last_us": time_avail_last,
            "overrun_count": overrun_count,
            "monitor_loop_delay_ms": int(values.get("rtt_dbg_monitor_loop_delay_ms") or 0),
            "monitor_loop_delay_max_ms": int(values.get("rtt_dbg_monitor_loop_delay_max_ms") or 0),
            "monitor_stuck_count": int(values.get("rtt_dbg_monitor_stuck_count") or 0),
            "monitor_stuck_task": int(values.get("rtt_dbg_monitor_stuck_task") or 0),
            "monitor_stuck_semline": int(values.get("rtt_dbg_monitor_stuck_semline") or 0),
            "scheduler_task_last_idx": task_last_idx,
            "scheduler_task_last_us": task_last_us,
            "scheduler_task_last_allowed_us": task_last_allowed_us,
            "scheduler_task_max_idx": task_max_idx,
            "scheduler_task_max_us": task_max_us,
            "scheduler_task_max_allowed_us": task_max_allowed_us,
            "scheduler_task_overrun_count": task_overrun_count,
            "scheduler_task_not_achieved_count": task_not_achieved_count,
            "scheduler_extra_loop_us": extra_loop_us,
            "gcs_global_update_send_total_us": gcs_global_total_us,
            "gcs_global_update_send_total_max_us": gcs_global_total_max_us,
            "gcs_update_send_total_us": gcs_total_us,
            "gcs_update_send_total_max_us": gcs_total_max_us,
            "gcs_update_send_log_us": gcs_log_us,
            "gcs_update_send_log_max_us": gcs_log_max_us,
            "gcs_update_send_check_tasks_us": gcs_check_tasks_us,
            "gcs_update_send_check_tasks_max_us": gcs_check_tasks_max_us,
            "gcs_update_send_loop_us": gcs_loop_us,
            "gcs_update_send_loop_max_us": gcs_loop_max_us,
            "gcs_update_send_loop_iters": int(values.get("rtt_dbg_gcs_update_send_loop_iters") or 0),
            "gcs_update_send_loop_iters_max": int(values.get("rtt_dbg_gcs_update_send_loop_iters_max") or 0),
            "gcs_service_statustext_us": int(values.get("rtt_dbg_gcs_service_statustext_us") or 0),
            "gcs_service_statustext_max_us": int(values.get("rtt_dbg_gcs_service_statustext_max_us") or 0),
            "gcs_service_statustext_sent": int(values.get("rtt_dbg_gcs_service_statustext_sent") or 0),
            "gcs_service_statustext_sent_max": int(values.get("rtt_dbg_gcs_service_statustext_sent_max") or 0),
            "gcs_service_statustext_budget_breaks": int(values.get("rtt_dbg_gcs_service_statustext_budget_breaks") or 0),
            "gcs_update_send_break_out_of_time": int(values.get("rtt_dbg_gcs_update_send_break_out_of_time") or 0),
            "gcs_update_send_break_try_send": int(values.get("rtt_dbg_gcs_update_send_break_try_send") or 0),
            "gcs_update_send_break_try_send_id": int(values.get("rtt_dbg_gcs_update_send_break_try_send_id") or 0),
            "gcs_update_send_last_txspace": int(values.get("rtt_dbg_gcs_update_send_last_txspace") or 0),
            "log_send_batch_us": int(values.get("rtt_dbg_log_send_batch_us") or 0),
            "log_send_batch_max_us": int(values.get("rtt_dbg_log_send_batch_max_us") or 0),
            "log_send_data_us": int(values.get("rtt_dbg_log_send_data_us") or 0),
            "log_send_data_max_us": int(values.get("rtt_dbg_log_send_data_max_us") or 0),
            "log_send_batch_sent": int(values.get("rtt_dbg_log_send_batch_sent") or 0),
            "log_send_batch_sent_max": int(values.get("rtt_dbg_log_send_batch_sent_max") or 0),
            "log_send_batch_budget_breaks": int(values.get("rtt_dbg_log_send_batch_budget_breaks") or 0),
            "log_send_batch_no_space_breaks": int(values.get("rtt_dbg_log_send_batch_no_space_breaks") or 0),
            "log_send_num_sends": int(values.get("rtt_dbg_log_send_num_sends") or 0),
            "slcan_report_calls": int(values.get("rtt_dbg_slcan_report_calls") or 0),
            "slcan_report_sent": int(values.get("rtt_dbg_slcan_report_sent") or 0),
            "slcan_report_no_space": int(values.get("rtt_dbg_slcan_report_no_space") or 0),
            "slcan_report_low_space_drops": int(values.get("rtt_dbg_slcan_report_low_space_drops") or 0),
            "slcan_report_us": int(values.get("rtt_dbg_slcan_report_us") or 0),
            "slcan_report_max_us": int(values.get("rtt_dbg_slcan_report_max_us") or 0),
            "copter_motors_total_us": int(values.get("rtt_dbg_copter_motors_total_us") or 0),
            "copter_motors_total_accum_us": int(values.get("rtt_dbg_copter_motors_total_accum_us") or 0),
            "copter_motors_total_max_us": int(values.get("rtt_dbg_copter_motors_total_max_us") or 0),
            "copter_motors_total_slow_count": int(values.get("rtt_dbg_copter_motors_total_slow_count") or 0),
            "copter_motors_calc_pwm_us": int(values.get("rtt_dbg_copter_motors_calc_pwm_us") or 0),
            "copter_motors_calc_pwm_accum_us": int(values.get("rtt_dbg_copter_motors_calc_pwm_accum_us") or 0),
            "copter_motors_calc_pwm_max_us": int(values.get("rtt_dbg_copter_motors_calc_pwm_max_us") or 0),
            "copter_motors_calc_pwm_slow_count": int(values.get("rtt_dbg_copter_motors_calc_pwm_slow_count") or 0),
            "copter_motors_cork_us": int(values.get("rtt_dbg_copter_motors_cork_us") or 0),
            "copter_motors_cork_accum_us": int(values.get("rtt_dbg_copter_motors_cork_accum_us") or 0),
            "copter_motors_cork_max_us": int(values.get("rtt_dbg_copter_motors_cork_max_us") or 0),
            "copter_motors_output_ch_us": int(values.get("rtt_dbg_copter_motors_output_ch_us") or 0),
            "copter_motors_output_ch_accum_us": int(values.get("rtt_dbg_copter_motors_output_ch_accum_us") or 0),
            "copter_motors_output_ch_max_us": int(values.get("rtt_dbg_copter_motors_output_ch_max_us") or 0),
            "copter_motors_output_ch_slow_count": int(values.get("rtt_dbg_copter_motors_output_ch_slow_count") or 0),
            "copter_motors_interlock_us": int(values.get("rtt_dbg_copter_motors_interlock_us") or 0),
            "copter_motors_interlock_accum_us": int(values.get("rtt_dbg_copter_motors_interlock_accum_us") or 0),
            "copter_motors_interlock_max_us": int(values.get("rtt_dbg_copter_motors_interlock_max_us") or 0),
            "copter_motors_flightmode_us": int(values.get("rtt_dbg_copter_motors_flightmode_us") or 0),
            "copter_motors_flightmode_accum_us": int(values.get("rtt_dbg_copter_motors_flightmode_accum_us") or 0),
            "copter_motors_flightmode_max_us": int(values.get("rtt_dbg_copter_motors_flightmode_max_us") or 0),
            "copter_motors_flightmode_slow_count": int(values.get("rtt_dbg_copter_motors_flightmode_slow_count") or 0),
            "copter_motors_push_us": int(values.get("rtt_dbg_copter_motors_push_us") or 0),
            "copter_motors_push_accum_us": int(values.get("rtt_dbg_copter_motors_push_accum_us") or 0),
            "copter_motors_push_max_us": int(values.get("rtt_dbg_copter_motors_push_max_us") or 0),
            "copter_motors_push_slow_count": int(values.get("rtt_dbg_copter_motors_push_slow_count") or 0),
            "copter_motors_main_calls": int(values.get("rtt_dbg_copter_motors_main_calls") or 0),
            "copter_read_ahrs_us": int(values.get("rtt_dbg_copter_read_ahrs_us") or 0),
            "copter_read_ahrs_accum_us": int(values.get("rtt_dbg_copter_read_ahrs_accum_us") or 0),
            "copter_read_ahrs_max_us": int(values.get("rtt_dbg_copter_read_ahrs_max_us") or 0),
            "copter_read_ahrs_slow_count": int(values.get("rtt_dbg_copter_read_ahrs_slow_count") or 0),
            "copter_read_ahrs_calls": int(values.get("rtt_dbg_copter_read_ahrs_calls") or 0),
            "ins_loop_rate": ins_loop_rate,
            "cpu_idle_pct": cpu_idle,
            "cpu_load_pct": cpu_load,
            "sys_status_load_latest": sys_load_latest,
        },
        "thresholds": thresholds,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "timestamp_utc": iso_now(),
        "platform": os.uname().sysname if hasattr(os, "uname") else "",
        "verdict": "RED",
    }
    if args.openocd_before_sample and not args.no_openocd:
        if args.openocd_wait_steady:
            payload["openocd_snapshot_before"] = wait_for_openocd_steady(args)
        else:
            payload["openocd_snapshot_before"] = openocd_snapshot(args)
    payload["mavlink"] = sample_mavlink(args)
    if args.no_openocd:
        payload["openocd_snapshot"] = {"verdict": "SKIPPED", "reason": "no_openocd"}
    else:
        payload["openocd_snapshot"] = openocd_snapshot(args)
    before = payload.get("openocd_snapshot_before", {})
    after = payload.get("openocd_snapshot", {})
    if isinstance(before, dict) and isinstance(after, dict):
        before_arrays = before.get("task_arrays", {})
        after_arrays = after.get("task_arrays", {})
        if isinstance(before_arrays, dict) and isinstance(after_arrays, dict):
            payload["openocd_task_delta_tops"] = task_counter_deltas(before_arrays, after_arrays)
            payload["openocd_thread_delta_tops"] = thread_counter_deltas(before_arrays, after_arrays)
            payload["openocd_scalar_deltas"] = scalar_deltas(before, after)
            payload["openocd_average_loop_rate"] = average_loop_rate(before, after)
    payload["classification"] = classify(payload, args)
    payload["verdict"] = payload["classification"]["verdict"]
    payload["reason"] = payload["classification"]["reason"]
    return payload


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--sample-s", type=float, default=25.0)
    parser.add_argument("--heartbeat-timeout", type=float, default=15.0)
    parser.add_argument("--param-timeout", type=float, default=8.0)
    parser.add_argument("--drain-s", type=float, default=0.5)
    parser.add_argument("--source-system", type=int, default=246)
    parser.add_argument("--skip-param-requests", action="store_true",
                        help="do not request parameters; useful to measure passive steady-state load")
    parser.add_argument("--skip-message-interval-requests", action="store_true",
                        help="do not alter stream rates; useful to measure passive steady-state load")
    parser.add_argument("--target-loop-rate", type=int, default=400)
    parser.add_argument("--min-loop-rate", type=float, default=380.0)
    parser.add_argument("--sample-wait-suspicious-us", type=int, default=3500)
    parser.add_argument("--elf", default=DEFAULT_ELF)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--openocd-timeout", type=int, default=20)
    parser.add_argument("--openocd-cfg", nargs="+", default=list(DEFAULT_OPENOCD_CFG))
    parser.add_argument("--no-openocd", action="store_true")
    parser.add_argument("--openocd-before-sample", action="store_true",
                        help="capture a baseline OpenOCD snapshot before MAVLink sampling and report task counter deltas")
    parser.add_argument("--openocd-wait-steady", action=argparse.BooleanOptionalAction, default=True,
                        help="wait for the main loop counter to advance before the baseline OpenOCD snapshot")
    parser.add_argument("--openocd-steady-timeout-s", type=float, default=45.0)
    parser.add_argument("--openocd-steady-poll-s", type=float, default=1.0)
    args = parser.parse_args()

    payload = run(args)
    outdir = Path(args.outdir)
    write_json(outdir / "loop_rate_gate.json", payload)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if payload["verdict"] == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
