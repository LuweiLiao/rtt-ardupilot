# continue019 multiround USB gate

Timestamp UTC: `2026-06-10T23:47:55.142873+00:00`
Port: `/dev/ttyACM1`
Verdict: `GREEN`

## Rounds

| round | param | duration | max gap | raw ftp | ftp/sd | peripheral |
|---:|---|---:|---:|---|---|---|
| 1 | PASS | 1.244 | 0.0272 | PASS | PASS | PASS |
| 2 | PASS | 1.196 | 0.0294 | PASS | PASS | PASS |
| 3 | PASS | 1.273 | 0.0561 | PASS | PASS | PASS |

## Notes

- This gate does not prove the whole goal by itself; it is the next runtime stress evidence.
- The final GDB counter snapshot is intentionally run after runtime rounds, without reset before reading counters.
- If the new early-XFRC-with-residue branch counters remain zero, that path is still not proven as the root fix.
