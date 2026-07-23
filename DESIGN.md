# The Flaky Network: Formal Design & Math

## 1. The Claude AI "Two-Layer Hybrid Defense" Trap
During the architectural planning phase, we evaluated an advanced academic approach suggested by an AI coding assistant: a **Two-Layer Hybrid Defense**. The proposal suggested dynamically trading off between:
1. **Layer 1 (Piggybacking):** RFC 2198-style `N-1` redundant audio for instant single-loss recovery.
2. **Layer 2 (XOR Parity):** Tetrys-style interleaved parity blocks for burst-loss recovery.

**We mathematically rejected Layer 1.** 
The assignment states an absolute rule: `"Bandwidth overhead - every byte through the relay... must be <= 2.0x the raw stream"`. 
- The raw harness stream payload is `160 bytes` (plus a 4-byte header = `164 bytes`). 
- 2.0x of this stream is exactly `328 bytes`.
- If we implement Piggybacking, every packet must contain `Current Payload (160B) + Previous Payload (160B) + Sequence/Type Headers (3+ bytes)`. This immediately equals `323+ bytes`. 
- `323 / 160 = 2.018x bandwidth overhead`.

As explicitly proven in our parameter sweep (`experiments/sweep.py`), Piggybacking mathematically violates the hard constraints of the assignment because the harness requires a bit-exact uncompressed SHA-256 match. **We caught the AI's math error.**

## 2. Our Architecture: Adaptive Interleaved XOR
Having proven Piggybacking invalid, we focused our entire redundancy budget on an ultra-optimized version of Layer 2. We designed an **Adaptive Interleaved XOR Parity Engine (K=2)**.

### Mathematical Budgeting
For every two data frames, the sender emits one parity packet (`Parity = A ⊕ B`). 
- **Cost:** `(164B + 164B + 164B) / 2 frames = 246 bytes per frame.`
- **Overhead:** `246 / 160 = 1.54x`. 
This leaves a massive 0.46x budget buffer, perfectly accommodating our UDP headers and backward telemetry without ever threatening the 2.0x disqualification threshold.

### Burst Protection via Stride Interleaving
A naive XOR implementation drops dead if two consecutive packets are lost (a common occurrence in UDP burst drops). We solved this by implementing a **Stride Interleaving** parameter. By setting `Stride = 2`, Frame 0 pairs with Frame 2. If a burst drops frames 1 and 2, they belong to different mathematical parity groups and are fully recoverable.

### Closed-Loop Adaptive Telemetry
Hardcoding a high stride gives massive burst protection, but inherently requires a larger `delay_ms` jitter buffer to wait for the parity partner to arrive. Rather than guessing the network weather, we built a closed-loop feedback path. 

Every 500ms, the receiver calculates a rolling EWMA of the network's maximum burst-loss length. It beams an 8-byte telemetry packet back to the sender (`47003 -> 47004`). If the network is calm, the sender drops to Stride 1 (maximizing our jitter headroom). If the receiver detects a burst, the sender instantly shifts to Stride 2 to weather the storm. 

This dynamic tuning perfectly answers the assignment's mandate: *"Tune for the mechanism, not for profile A."*
