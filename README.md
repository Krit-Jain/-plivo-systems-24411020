# The Flaky Network — FEC-Protected Real-Time Transport

A high-performance sender/receiver pair that transports live audio frames across
a hostile UDP relay (packet loss, delay, reordering, duplication) with minimal
added latency, using **forward error correction** (FEC).

## Quick Start

```bash
make clean && make
python3 run.py --profile profiles/A.json --delay_ms 60
python3 run.py --profile profiles/B.json --delay_ms 100
```

## Architecture

```
harness source ──47010──▶ SENDER ──47001──▶ relay ──47002──▶ RECEIVER ──47020──▶ player
                              │                                  │
                              │  XOR parity (K=2, stride S)      │  Immediate forward
                              │  interleaved groups               │  + FEC recovery
                              └──────── 47004 ◀── relay ◀── 47003┘
                                        (feedback path)
```

### FEC Strategy

- **Interleaved XOR parity** with K=2 (pairs) and configurable stride S
- Every pair of frames produces one parity packet: `P = frame_i ⊕ frame_{i+S}`
- Stride-based interleaving ensures consecutive burst losses land in different
  parity groups — each independently recoverable
- Zero-delay forwarding: DATA frames are delivered to the player instantly on
  arrival; FEC recovery triggers only for missing frames

### Wire Protocol

| Byte(s) | DATA packet | PARITY packet |
|---------|------------|---------------|
| 0       | type (0x01) | type (0x02)  |
| 1       | stride      | stride       |
| 2–3     | frame seq (BE) | base_seq (BE) |
| 4–163   | payload (160B) | XOR parity (160B) |

**Total: 164 bytes** — same size as harness format for minimal overhead.

### Budget

```
Data:    1500 × 164 = 246,000 B
Parity:   750 × 164 = 123,000 B
Total:               = 369,000 B → 1.54× overhead (cap: 2.0×)
```

## Results

| Profile | delay_ms | Miss Rate | Overhead | Result |
|---------|----------|-----------|----------|--------|
| A (2% loss) | 60 ms | 0.00–0.27% | 1.54× | ✅ VALID |
| B (5% loss) | 100 ms | 0.20–0.80% | 1.54× | ✅ VALID |

## Files

| File | Purpose |
|------|---------|
| `sender.cpp` | FEC sender — ingests harness frames, emits DATA + PARITY |
| `receiver.cpp` | FEC receiver — immediate delivery + XOR recovery |
| `protocol.hpp` | Wire format definitions and encode/decode helpers |
| `fec.hpp` | XOR engine and stride-based pair mapping |
| `net.hpp` | Non-blocking UDP socket utilities |
| `Makefile` | Builds `./sender` and `./receiver` (C++17) |

## Language

C++17, standard library only (sockets, threads, time). No external dependencies.
