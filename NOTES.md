# Architectural Notes

1. **Grading Target:** Please evaluate our submission at exactly `delay_ms = 120`. 

2. **Core Design:** Our architecture leverages an Adaptive Interleaved Forward Error Correction (FEC) mechanism using proactive XOR parity (K=2). 
3. Rather than relying on slow NACK retransmissions, the sender proactively calculates and transmits one parity packet for every two data frames. 
4. This yields a deterministic `1.54x` bandwidth overhead, successfully avoiding the strict `2.00x` disqualification threshold while granting robust mathematical packet recovery. 
5. To defend the audio stream against consecutive burst losses, the mathematical parity pairings are interleaved by an adjustable "Stride" parameter.

6. **Adaptive Telemetry:** The receiver continuously monitors incoming packet sequences over a 500ms sliding window to calculate the maximum network burst-loss length. 
7. It beams this telemetry back to the sender via a lightweight 8-byte UDP packet (`480 bytes/sec` overhead). 
8. The sender utilizes this telemetry to dynamically shift between Stride 1 (maximizing jitter buffer headroom on clean networks) and Stride 2 (maximizing burst protection on volatile networks).

9. **Failure Conditions:** This architecture will break if a network burst exceeds 2 consecutive packet drops, as our fixed `120ms` delay budget cannot physically accommodate the latency required to wait for Stride 3 parity packets. 
10. Furthermore, the system will fail if the raw packet loss permanently exceeds 33%, overwhelming the K=2 mathematical recovery capability.
