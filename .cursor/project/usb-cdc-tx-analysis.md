# USB CDC TX Path Analysis — PARAM_REQUEST_READ txspace()=0

## Full TX Path

```
mavlink_msg_send()
  → HAVE_PAYLOAD_SPACE check → UARTDriver::txspace() → _writebuf.space()
  → UARTDriver::_write() → _writebuf.write(buf, len)
  → [1kHz ap_uart thread] UARTDriver::_timer_tick()
    → _drain_writebuf_to_dev()
      → _writebuf.peekbytes(_tx_bounce[512], 512)
      → rt_device_write(_dev, 0, _tx_bounce, n)
        → usbd_serial_write()
          → rt_ringbuffer_put(&serial->tx_rb, buf, size)   // 4096B ringbuffer
          → usbd_serial_kick_tx()
            → tx_active guard (PRIMASK atomic)
            → pre-flight EPENA check
            → rt_ringbuffer_get(&tx_rb, tx_pkt[64], min(avail, mps))  // mps=64
            → usbd_ep_start_write(busid, in_ep, tx_pkt, got)
              → [USB DWC2 hardware: IN transfer, 1 packet ≤64 bytes]
    → [USB XFRC ISR] usbd_cdc_acm_bulk_in()
      → tx_active = 0
      → usbd_serial_kick_tx()   // chain next packet
```

## Buffer Sizes

| Buffer | Size | Notes |
|--------|------|-------|
| `_writebuf` (AP_HAL) | 8192 B | UARTDriver ring buffer |
| `_tx_bounce` | 512 B | Per-tick drain chunk |
| `tx_rb` (CherryUSB) | 4096 B | USB CDC TX ring buffer |
| `tx_pkt` | 64 B | Single USB packet buffer |
| USB FS bulk MPS | 64 B | Max per-packet |

## Root Cause

**USB FS bulk throughput (64 KB/s) cannot keep up with MAVLink traffic generation rate.**

The chain of events:

1. `_drain_writebuf_to_dev()` runs at 1kHz, pushes up to 512B/tick into `tx_rb` (4096B)
2. `usbd_serial_kick_tx()` sends **one 64-byte packet per USB frame** (1ms USB frame = 64 packets/s max)
3. `tx_rb` drains at 64B/1ms = 64 KB/s
4. `_drain_writebuf_to_dev()` feeds `tx_rb` at 512B/1ms = 512 KB/s
5. **`tx_rb` fills in ~8ms** (4096/512), then `rt_ringbuffer_put()` returns 0
6. Drain stops → `_writebuf` fills in ~16ms (8192/512)
7. **`txspace()` returns 0** → `HAVE_PAYLOAD_SPACE` rejects PARAM_REQUEST_READ

## Why Previous Fixes Didn't Work

| Attempt | Why it failed |
|---------|---------------|
| Drain in txspace() | tx_rb is still full; drain just fails immediately |
| Direct send | Same bottleneck — tx_rb full, rt_ringbuffer_put returns 0 |
| Bypass HAVE_PAYLOAD_SPACE | `_write()` → `_writebuf.write()` still fails when _writebuf is full |
| MSG_NEXT_PARAM scheduling | Doesn't help if buffer is perpetually full |

## The Real Problem

The USB CDC `tx_rb` (4096B) is a **funnel bottleneck**: it accepts data 8× faster than it can transmit. Once full, the entire TX chain backs up.

Additionally, `usbd_serial_kick_tx()` only sends **one MPS-sized packet** per call. It chains from the XFRC ISR, but each packet takes ~1ms USB frame. Even with perfect chaining, max throughput is 64 KB/s — far below typical MAVLink stream traffic.

## Possible Solutions

### Option A: Increase `tx_rb` to absorb bursts (band-aid)
```c
// usbd_serial.c
#define CONFIG_USBDEV_SERIAL_TX_BUFSIZE (16384)  // or larger
```
Delays but doesn't solve the fundamental throughput mismatch.

### Option B: Priority queue for parameter responses
Give PARAM_VALUE messages priority over STREAM_RAW etc. This requires AP-level changes to the MAVLink send path — not trivial.

### Option C: Throttle MAVLink stream rates to fit USB bandwidth
Reduce `SRx` parameters so total MAVLink throughput stays under ~50 KB/s (leaving headroom for params).

### Option D: Increase drain rate / larger packets
Not possible on USB FS — MPS is fixed at 64B. On USB HS would be 512B (8× improvement).

### Option E: Dedicated param response path (recommended)
When a PARAM_REQUEST_READ arrives, temporarily pause MAVLink streams, send the param response, then resume. This guarantees the param response gets through regardless of buffer state.
