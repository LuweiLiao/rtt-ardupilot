# Counter Decode After Monotonic-Time GREEN Gate

Source: `openocd_mdw_after_green.stderr` / `.stdout`

| item | symbol | addr | value |
|---|---|---:|---:|
| Scheduler wait sample last us | `rtt_dbg_scheduler_wait_sample_last_us` | 0x2002007c | 268 |
| Scheduler wait sample max us | `rtt_dbg_scheduler_wait_sample_max_us` | 0x20020080 | 16871 |
| Scheduler wait sample large count | `rtt_dbg_scheduler_wait_sample_large_count` | 0x20020084 | 0 |
| Scheduler run time available last us | `rtt_dbg_scheduler_run_time_available_last_us` | 0x20020088 | 2497 |
| Scheduler run time available min us | `rtt_dbg_scheduler_run_time_available_min_us` | 0x2002008c | 0 |
| GCS update_send calls | `rtt_dbg_gcs_update_send_calls` | 0x2002021c | 411651 |
| GCS out_of_time breaks | `rtt_dbg_gcs_update_send_break_out_of_time` | 0x20020248 | 15125 |
| GCS overtime grace total | `rtt_dbg_gcs_update_send_overtime_grace` | 0x20020238 | 9802 |
| GCS overtime denied | `rtt_dbg_gcs_update_send_overtime_grace_denied` | 0x20020240 | 314 |
| GCS param overtime active | `rtt_dbg_gcs_update_send_param_overtime_active` | 0x20020234 | 564 |
| GCS param overtime granted | `rtt_dbg_gcs_update_send_param_overtime_granted` | 0x2002023c | 557 |
| GCS param overtime denied | `rtt_dbg_gcs_update_send_param_overtime_denied` | 0x20020244 | 7 |
| GCS param priority due | `rtt_dbg_gcs_update_send_param_priority_due` | 0x20020254 | 746 |
| GCS param priority sent | `rtt_dbg_gcs_update_send_param_priority_sent` | 0x20020264 | 746 |
| GCS param priority try false | `rtt_dbg_gcs_update_send_param_priority_try_false` | 0x20020258 | 0 |
| GCS param catchup | `rtt_dbg_gcs_update_send_param_catchup` | 0x20020268 | 83 |
| GCS param pushed deferred | `rtt_dbg_gcs_update_send_param_pushed_deferred` | 0x20020274 | 1429 |
| GCS chan0 update gap max ms | `rtt_dbg_gcs_chan0_update_send_gap_max_ms` | 0x20020214 | 342 |
| GCS chan0 update gap large count | `rtt_dbg_gcs_chan0_update_send_gap_large_count` | 0x20020218 | 8 |
| GCS global update gap max ms | `rtt_dbg_gcs_global_update_send_gap_max_ms` | 0x20020288 | 338 |
| GCS global update gap large count | `rtt_dbg_gcs_global_update_send_gap_large_count` | 0x2002028c | 8 |
| PARAM request list count | `rtt_dbg_gcs_param_request_list_count` | 0x200202ec | 3 |
| PARAM active windows opened | `rtt_dbg_gcs_param_active_window_opened` | 0x200202fc | 3 |
| PARAM queued calls | `rtt_dbg_gcs_param_queued_calls` | 0x20020304 | 765 |
| PARAM completed | `rtt_dbg_gcs_param_completed` | 0x20020380 | 3 |
| PARAM stream sent total | `rtt_dbg_gcs_param_stream_sent_total` | 0x20020390 | 2736 |
| PARAM call gap max ms | `rtt_dbg_gcs_param_call_gap_max_ms` | 0x20020314 | 71392 |
| PARAM call gap large count | `rtt_dbg_gcs_param_call_gap_large_count` | 0x20020318 | 0 |
| PARAM send gap max ms | `rtt_dbg_gcs_param_send_gap_max_ms` | 0x20020368 | 56 |
| PARAM send gap large count | `rtt_dbg_gcs_param_send_gap_large_count` | 0x2002036c | 0 |
| PARAM send gap before index | `rtt_dbg_gcs_param_send_gap_before_index` | 0x20020370 | 0 |
| PARAM send gap after index | `rtt_dbg_gcs_param_send_gap_after_index` | 0x20020374 | 0 |
| PARAM last stream elapsed us | `rtt_dbg_gcs_param_last_stream_elapsed_us` | 0x2002035c | 301 |
| PARAM max stream elapsed us | `rtt_dbg_gcs_param_max_stream_elapsed_us` | 0x20020384 | 5199 |
| PARAM last time budget us | `rtt_dbg_gcs_param_last_time_budget_us` | 0x20020360 | 2500 |
| PARAM last quantum count | `rtt_dbg_gcs_param_last_quantum_count` | 0x20020334 | 4 |
| PARAM quantum caps | `rtt_dbg_gcs_param_quantum_caps` | 0x20020330 | 703 |
| PARAM time breaks | `rtt_dbg_gcs_param_time_breaks` | 0x20020378 | 155 |
| PARAM txbuf breaks | `rtt_dbg_gcs_param_txbuf_breaks` | 0x2002037c | 0 |
| PARAM last txspace | `rtt_dbg_gcs_param_last_txspace` | 0x2002033c | 2047 |
| PARAM delay pump calls | `rtt_dbg_gcs_param_delay_pump_calls` | 0x20020398 | 371 |
| IOMCU status error resets | `rtt_dbg_iomcu_status_error_resets` | 0x20020450 | 0 |
| UART txspace calls | `rtt_uart_dbg_usb_txspace_calls` | 0x20020c68 | 224520 |
| UART min writebuf space | `rtt_uart_dbg_usb_txspace_min_writebuf_space` | 0x20020c78 | 18 |
| UART min lld space | `rtt_uart_dbg_usb_txspace_min_lld_space` | 0x20020c7c | 0 |
| UART drain calls | `rtt_uart_dbg_drain_calls` | 0x20020c88 | 28299 |
| UART drain USB full | `rtt_uart_dbg_drain_usb_full` | 0x20020c90 | 15559 |
| UART drain bytes | `rtt_uart_dbg_drain_bytes` | 0x20020c98 | 394231 |
| UART drain writes | `rtt_uart_dbg_drain_writes` | 0x20020c9c | 14189 |
| UART write calls | `rtt_uart_dbg_usb_write_calls` | 0x20020ccc | 17947 |
| UART write wait ms | `rtt_uart_dbg_usb_write_wait_ms` | 0x20020cdc | 0 |
| UART write max wait ms | `rtt_uart_dbg_usb_write_max_wait_ms` | 0x20020ce0 | 0 |
| UART write no space | `rtt_uart_dbg_usb_write_no_space` | 0x20020ce4 | 0 |
| UART write short | `rtt_uart_dbg_usb_write_short` | 0x20020ce8 | 0 |
| Cherry DTR | `rtt_dbg_cherry_dtr_state` | 0x20021578 | 0 |
| Cherry bulk in calls | `rtt_dbg_cherry_bulk_in_calls` | 0x200215ac | 6503 |
| Cherry start ok | `rtt_dbg_cherry_tx_start_ok` | 0x200215b4 | 6389 |
| Cherry start fail | `rtt_dbg_cherry_tx_start_fail` | 0x200215b0 | 0 |
| Cherry arm bytes | `rtt_dbg_cherry_tx_arm_bytes` | 0x200215bc | 299083 |
| Cherry complete bytes | `rtt_dbg_cherry_tx_complete_bytes` | 0x2002165c | 299083 |
| Cherry ring dropped | `rtt_dbg_cherry_tx_ring_dropped` | 0x200216fc | 0 |
| Cherry ring high water | `rtt_dbg_cherry_tx_ring_high_water` | 0x20021704 | 13 |
| Cherry ring enqueued | `rtt_dbg_cherry_tx_ring_enqueued` | 0x20021708 | 7487 |
| Cherry completion assumed | `rtt_dbg_cherry_tx_completion_assumed` | 0x20021640 | 0 |
| Cherry epdis recovery | `rtt_dbg_cherry_epdis_recovery_count` | 0x20021650 | 0 |
| DWC2 start write | `rtt_dbg_dwc2_ep1_start_write_calls` | 0x20021f80 | 6503 |
| DWC2 immediate prime calls | `rtt_dbg_dwc2_ep1_immediate_prime_calls` | 0x20021f88 | 6389 |
| DWC2 immediate prime wrote | `rtt_dbg_dwc2_ep1_immediate_prime_wrote` | 0x20021f8c | 6389 |
| DWC2 immediate prime bytes | `rtt_dbg_dwc2_ep1_immediate_prime_bytes` | 0x20021f90 | 299083 |
| DWC2 immediate prime zero | `rtt_dbg_dwc2_ep1_immediate_prime_zero` | 0x20021f94 | 0 |
| DWC2 TXFE | `rtt_dbg_dwc2_ep1_txfe` | 0x20021fe0 | 0 |
| DWC2 XFRC | `rtt_dbg_dwc2_ep1_xfrc` | 0x20022048 | 6503 |
| DWC2 complete calls | `rtt_dbg_dwc2_ep1_complete_calls` | 0x2002204c | 6503 |
| DWC2 actual last | `rtt_dbg_dwc2_ep1_actual_last` | 0x20022050 | 41 |
| DWC2 incomplete ignored | `rtt_dbg_dwc2_ep1_xfrc_incomplete_ignored` | 0x20021fe8 | 0 |
| DWC2 incomplete txfe rearmed | `rtt_dbg_dwc2_ep1_xfrc_incomplete_txfe_rearmed` | 0x20021ff8 | 0 |
| DWC2 deferred until drained | `rtt_dbg_dwc2_ep1_xfrc_deferred_until_drained` | 0x20021ffc | 0 |
| DWC2 fifo residue complete | `rtt_dbg_dwc2_ep1_xfrc_complete_after_fifo_load_with_residue` | 0x20022000 | 0 |

## Interpretation

- Reliable monotonic-time gate result: `multiround_usb_gate.rc=0`; all three rounds passed PARAM, raw FTP, strict FTP/SD, and peripheral checks.
- PARAM max send gap counter is 56 ms with large_count=0; host summary max gaps were 27.2 ms, 29.4 ms, and 56.1 ms.
- PARAM max stream elapsed is 5199 us; the previous RED gate had an underflowed 0xffffffc4 value, so the monotonic DWT accumulator removed the observed time-source reversal.
- Low-level conservation: Cherry bulk_in=6503, DWC2 start=6503, XFRC=6503, complete=6503; Cherry arm bytes=299083 and complete bytes=299083.
- Error paths stayed quiet: Cherry start_fail=0, ring_dropped=0, completion_assumed=0, epdis_recovery=0; DWC2 incomplete/deferred/residue counters=0/0/0.
- UART producer wait/short paths stayed inactive: wait_ms=0, no_space=0, short=0.
