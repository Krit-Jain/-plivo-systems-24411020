# The Flaky Network: Forward Error Correction Transport

A high-performance sender/receiver pair that transports live audio frames across a hostile UDP relay (featuring packet loss, severe jitter, reordering, and duplication). By implementing an Adaptive Interleaved Forward Error Correction (FEC) system, we achieve robust playout survival with minimal added latency.

## Quick Start

```bash
# Build the C++17 binaries
make clean && make

# Run on practice profiles
python3 run.py --profile profiles/A.json --delay_ms 60
python3 run.py --profile profiles/B.json --delay_ms 120
```

## Architecture & Telemetry

```mermaid
graph LR
    A[Harness Source] -- 47010 --> B(C++ Sender)
    B -- 47001 Data+Parity --> C{Hostile Relay}
    C -- 47002 --> D(C++ Receiver)
    D -- 47020 --> E[Harness Player]
    D -. 47003 Telemetry .-> C
    C -. 47004 .-> B
    
    style B fill:#1e293b,stroke:#38bdf8,stroke-width:2px,color:#fff
    style D fill:#1e293b,stroke:#38bdf8,stroke-width:2px,color:#fff
    style C fill:#471323,stroke:#ef4444,stroke-width:2px,color:#fff
```

### FEC Strategy (Interleaved XOR)
Naive retransmission (NACKs) violate real-time speed-of-light constraints due to double round-trip times. We implemented proactive Interleaved XOR Parity (K=2).

1. Parity Generation: Every 2 data frames generate 1 parity packet (P = Frame_A XOR Frame_B).
2. Burst Protection: We interleave the pairs based on a Stride parameter. If Stride = 2, Frame 0 pairs with Frame 2. A network burst dropping consecutive frames 1 and 2 safely isolates the losses into separate parity groups, allowing full mathematical recovery.
3. Adaptive Feedback: The receiver monitors network health over 500ms sliding windows. It sends an 8-byte telemetry packet back to the sender, dynamically instructing it to shift between Stride 1 (low delay) and Stride 2 (heavy burst protection) based on real-time packet loss patterns.
4. Perpetual Ring Buffers: The internal C++ architecture uses modulo-indexed Ring Buffers (`seq % 4096`) with proactive state-clearing, guaranteeing zero-allocation memory safety for infinite streams without overflowing.

### Wire Protocol

To strictly enforce the < 2.0x bandwidth budget, the custom protocol is optimized to precisely 164 bytes per packet.

```
[0-7]    Type (DATA=0x01, PARITY=0x02, FEEDBACK=0x03)
[8-15]   Stride (Adaptive S)
[16-31]  Sequence (BE)
[32-1311] Payload / XOR Parity (160 Bytes)
```

### Mathematical Budget

- Data Frames: 1500 * 164 bytes = 246,000 B
- Parity Frames: 750 * 164 bytes = 123,000 B
- Telemetry Feedback: 60 * 8 bytes = 480 B
- Total Overhead: ~1.54x (Well under the strict 2.00x limit)

## Verified Results & Grading Target

We request to be graded at a Play-out Delay of 120ms.
This delay permits our Adaptive Stride=2 logic 40ms to generate parity packets, while retaining an 80ms buffer specifically dedicated to surviving severe network jitter spikes.

| Profile | delay_ms | Miss Rate | Overhead | Result |
|---------|----------|-----------|----------|--------|
| A (Low Loss) | 60 ms | ~0.13% | 1.54x | VALID |
| B (High Loss) | 120 ms | ~0.13% | 1.54x | VALID |
| B (Adaptive) | 120 ms | ~0.80% | 1.54x | VALID |

## Repository Structure

- sender.cpp: Ingests frames, executes the Adaptive FEC encoder, emits DATA/PARITY.
- receiver.cpp: Executes Zero-Delay Forwarding, XOR parity recovery, and generates telemetry.
- protocol.hpp: Bit-level wire format definitions.
- fec.hpp: The XOR mathematical engine.
- RUNLOG.md: Experimental timeline and engineering rationale.
- NOTES.md: The concise 10-sentence technical grading summary.
- SUMMARY.html: Architecture visualization.
