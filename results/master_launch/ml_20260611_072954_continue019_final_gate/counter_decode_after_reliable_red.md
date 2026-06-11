# Counter Decode After Reliable RED Gate

Source: `openocd_mdw_after_reliable_red.stderr` / `.stdout`

| item | symbol | addr | value |
|---|---|---:|---:|
| GCS update_send calls | `rtt_dbg_gcs_update_send_calls` | 0x2002021c | 200361 |
| GCS out_of_time breaks | `rtt_dbg_gcs_update_send_break_out_of_time` | 0x20020248 | 39863 |
| GCS overtime grace total | `rtt_dbg_gcs_update_send_overtime_grace` | 0x20020238 | 26363 |
| GCS overtime denied | `rtt_dbg_gcs_update_send_overtime_grace_denied` | 0x20020240 | 1168 |
| GCS param overtime active | `rtt_dbg_gcs_update_send_param_overtime_active` | 0x20020234 | 736 |
| GCS param overtime granted | `rtt_dbg_gcs_update_send_param_overtime_granted` | 0x2002023c | 709 |
| GCS param overtime denied | `rtt_dbg_gcs_update_send_param_overtime_denied` | 0x20020244 | 27 |
| GCS param priority due | `rtt_dbg_gcs_update_send_param_priority_due` | 0x20020254 | 2411 |
| GCS param priority sent | `rtt_dbg_gcs_update_send_param_priority_sent` | 0x20020264 | 2411 |
| GCS param priority try false | `rtt_dbg_gcs_update_send_param_priority_try_false` | 0x20020258 | 0 |
| GCS param catchup | `rtt_dbg_gcs_update_send_param_catchup` | 0x20020268 | 786 |
| GCS param pushed deferred | `rtt_dbg_gcs_update_send_param_pushed_deferred` | 0x20020274 | 2523 |
| GCS chan0 update gap max ms | `rtt_dbg_gcs_chan0_update_send_gap_max_ms` | 0x20020214 | 6373 |
| GCS chan0 update gap large count | `rtt_dbg_gcs_chan0_update_send_gap_large_count` | 0x20020218 | 262 |
| GCS global update gap max ms | `rtt_dbg_gcs_global_update_send_gap_max_ms` | 0x20020288 | 6376 |
| PARAM request list count | `rtt_dbg_gcs_param_request_list_count` | 0x200202ec | 6 |
| PARAM active windows opened | `rtt_dbg_gcs_param_active_window_opened` | 0x200202fc | 6 |
| PARAM queued calls | `rtt_dbg_gcs_param_queued_calls` | 0x20020304 | 2443 |
| PARAM completed | `rtt_dbg_gcs_param_completed` | 0x20020380 | 6 |
| PARAM stream sent total | `rtt_dbg_gcs_param_stream_sent_total` | 0x20020390 | 5472 |
| PARAM call gap max ms | `rtt_dbg_gcs_param_call_gap_max_ms` | 0x20020314 | 104297 |
| PARAM call gap large count | `rtt_dbg_gcs_param_call_gap_large_count` | 0x20020318 | 1 |
| PARAM send gap max ms | `rtt_dbg_gcs_param_send_gap_max_ms` | 0x20020368 | 322 |
| PARAM send gap large count | `rtt_dbg_gcs_param_send_gap_large_count` | 0x2002036c | 1 |
| PARAM send gap before index | `rtt_dbg_gcs_param_send_gap_before_index` | 0x20020370 | 132 |
| PARAM send gap after index | `rtt_dbg_gcs_param_send_gap_after_index` | 0x20020374 | 133 |
| PARAM last quantum count | `rtt_dbg_gcs_param_last_quantum_count` | 0x20020334 | 4 |
| PARAM quantum caps | `rtt_dbg_gcs_param_quantum_caps` | 0x20020330 | 1659 |
| PARAM time breaks | `rtt_dbg_gcs_param_time_breaks` | 0x20020378 | 1089 |
| PARAM txbuf breaks | `rtt_dbg_gcs_param_txbuf_breaks` | 0x2002037c | 0 |
| PARAM last txspace | `rtt_dbg_gcs_param_last_txspace` | 0x2002033c | 2047 |
| IOMCU status error resets | `rtt_dbg_iomcu_status_error_resets` | 0x20020450 | 0 |
| UART txspace calls | `rtt_uart_dbg_usb_txspace_calls` | 0x20020c68 | 204769 |
| UART min writebuf space | `rtt_uart_dbg_usb_txspace_min_writebuf_space` | 0x20020c78 | 3 |
| UART min lld space | `rtt_uart_dbg_usb_txspace_min_lld_space` | 0x20020c7c | 0 |
| UART drain calls | `rtt_uart_dbg_drain_calls` | 0x20020c88 | 67020 |
| UART drain USB full | `rtt_uart_dbg_drain_usb_full` | 0x20020c90 | 44434 |
| UART drain bytes | `rtt_uart_dbg_drain_bytes` | 0x20020c98 | 643617 |
| UART drain writes | `rtt_uart_dbg_drain_writes` | 0x20020c9c | 24148 |
| UART write calls | `rtt_uart_dbg_usb_write_calls` | 0x20020ccc | 33198 |
| UART write wait ms | `rtt_uart_dbg_usb_write_wait_ms` | 0x20020cdc | 0 |
| UART write max wait ms | `rtt_uart_dbg_usb_write_max_wait_ms` | 0x20020ce0 | 0 |
| UART write no space | `rtt_uart_dbg_usb_write_no_space` | 0x20020ce4 | 0 |
| UART write short | `rtt_uart_dbg_usb_write_short` | 0x20020ce8 | 0 |
| Cherry configured | `rtt_dbg_cherry_configured_state` | 0x20020d60 | MISSING |
| Cherry DTR | `rtt_dbg_cherry_dtr_state` | 0x20021578 | 0 |
| Cherry bulk in calls | `rtt_dbg_cherry_bulk_in_calls` | 0x200215ac | 11521 |
| Cherry start ok | `rtt_dbg_cherry_tx_start_ok` | 0x200215b4 | 11414 |
| Cherry start fail | `rtt_dbg_cherry_tx_start_fail` | 0x200215b0 | 0 |
| Cherry arm bytes | `rtt_dbg_cherry_tx_arm_bytes` | 0x200215bc | 484044 |
| Cherry complete bytes | `rtt_dbg_cherry_tx_complete_bytes` | 0x2002165c | 484044 |
| Cherry ring dropped | `rtt_dbg_cherry_tx_ring_dropped` | 0x200216fc | 0 |
| Cherry ring high water | `rtt_dbg_cherry_tx_ring_high_water` | 0x20021704 | 8 |
| Cherry ring enqueued | `rtt_dbg_cherry_tx_ring_enqueued` | 0x20021708 | 12587 |
| Cherry completion assumed | `rtt_dbg_cherry_tx_completion_assumed` | 0x20021640 | 0 |
| Cherry epdis recovery | `rtt_dbg_cherry_epdis_recovery_count` | 0x20021650 | 0 |
| DWC2 start write | `rtt_dbg_dwc2_ep1_start_write_calls` | 0x20021f80 | 11521 |
| DWC2 immediate prime calls | `rtt_dbg_dwc2_ep1_immediate_prime_calls` | 0x20021f88 | 11414 |
| DWC2 immediate prime wrote | `rtt_dbg_dwc2_ep1_immediate_prime_wrote` | 0x20021f8c | 11414 |
| DWC2 immediate prime bytes | `rtt_dbg_dwc2_ep1_immediate_prime_bytes` | 0x20021f90 | 484044 |
| DWC2 immediate prime zero | `rtt_dbg_dwc2_ep1_immediate_prime_zero` | 0x20021f94 | 0 |
| DWC2 TXFE | `rtt_dbg_dwc2_ep1_txfe` | 0x20021fe0 | 0 |
| DWC2 XFRC | `rtt_dbg_dwc2_ep1_xfrc` | 0x20022048 | 11521 |
| DWC2 complete calls | `rtt_dbg_dwc2_ep1_complete_calls` | 0x2002204c | 11521 |
| DWC2 actual last | `rtt_dbg_dwc2_ep1_actual_last` | 0x20022050 | 21 |
| DWC2 incomplete ignored | `rtt_dbg_dwc2_ep1_xfrc_incomplete_ignored` | 0x20021fe8 | 0 |
| DWC2 incomplete txfe rearmed | `rtt_dbg_dwc2_ep1_xfrc_incomplete_txfe_rearmed` | 0x20021ff8 | 0 |
| DWC2 deferred until drained | `rtt_dbg_dwc2_ep1_xfrc_deferred_until_drained` | 0x20021ffc | 0 |
| DWC2 fifo residue complete | `rtt_dbg_dwc2_ep1_xfrc_complete_after_fifo_load_with_residue` | 0x20022000 | 0 |

## Interpretation

- Reliable gate result: `multiround_reliable.rc=2`; PARAM coverage stayed 912/912 but round 3 max gap was 0.322s.
- Low-level conservation snapshot: Cherry bulk_in=11521, DWC2 start=11521, XFRC=11521, complete=11521.
- Cherry errors: start_fail=0, ring_dropped=0, completion_assumed=0, epdis_recovery=0.
- UART producer wait/short paths: wait_ms=0, no_space=0, short=0.
- GCS/PARAM gap counters: param_call_gap_max_ms=104297, param_call_gap_large_count=1, param_send_gap_max_ms=322, param_send_gap_large_count=1, before_index=132, after_index=133.
