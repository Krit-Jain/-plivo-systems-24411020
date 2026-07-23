# Architectural Notes

**Grading Target:** Please evaluate our submission at exactly `delay_ms = 120`. 

**Core Design:** Our architecture leverages an Adaptive Interleaved Forward Error Correction (FEC) mechanism using proactive XOR parity (K=2), calculating and transmitting one parity packet for every two data frames rather than relying on slow NACK retransmissions. This yields a deterministic `1.54x` bandwidth overhead, successfully avoiding the strict `2.00x` disqualification threshold while granting robust mathematical packet recovery. To defend the audio stream against consecutive burst losses, the mathematical parity pairings are interleaved by an adjustable "Stride" parameter.

**Adaptive Telemetry:** The receiver continuously monitors incoming packet sequences over a 500ms sliding window to calculate the maximum network burst-loss length. It beams this telemetry back to the sender via a lightweight 8-byte UDP packet (`480 bytes/sec` overhead). The sender utilizes this telemetry to dynamically shift between Stride 1 (maximizing jitter buffer headroom on clean networks) and Stride 2 (maximizing burst protection on volatile networks).

**Memory Architecture:** Both endpoints utilize modulo-indexed Perpetual Ring Buffers (`seq % 4096`) with proactive state-clearing. This guarantees the zero-allocation C++ engines can process infinite streams (e.g., stress tests > 81 seconds) without overflowing static boundaries. 

**Failure Conditions:** This architecture will break if a network burst exceeds 2 consecutive packet drops, as our fixed `120ms` delay budget cannot physically accommodate the latency required to wait for Stride 3 parity packets. Furthermore, the system will fail if the raw packet loss permanently exceeds 33%, overwhelming the K=2 mathematical recovery capability.
