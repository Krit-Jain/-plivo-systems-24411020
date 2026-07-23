/*
 * fec.hpp — Forward Error Correction engine (XOR-based, K=2 parity groups).
 *
 * Grouping strategy (stride-interleaved, non-overlapping pairs):
 *
 *   stride S=1: {0,1}, {2,3}, {4,5}, ...
 *   stride S=2: {0,2}, {1,3}, {4,6}, {5,7}, ...
 *   stride S=3: {0,3}, {1,4}, {2,5}, {6,9}, {7,10}, ...
 *
 * General rule: frames are divided into super-blocks of size 2S.
 * Within each super-block, frame at offset j (0 ≤ j < S) pairs with
 * frame at offset j+S. Parity is computed when the "late" partner
 * (offset ≥ S) arrives at the sender.
 *
 * Burst protection: stride S tolerates any burst of up to S consecutive
 * losses, because no two consecutive frames share a parity group.
 */

#ifndef FEC_HPP
#define FEC_HPP

#include "protocol.hpp"
#include <cstdint>
#include <cstring>

/* ── XOR computation ───────────────────────────────────────────────────── */

/*
 * XOR two PAYLOAD_BYTES payloads: out[i] = a[i] ^ b[i].
 * Uses 64-bit words for efficiency (160 = 20 × 8, no remainder).
 */
inline void xor_payloads(uint8_t* out,
                         const uint8_t* a,
                         const uint8_t* b) {
    const uint64_t* pa = reinterpret_cast<const uint64_t*>(a);
    const uint64_t* pb = reinterpret_cast<const uint64_t*>(b);
    uint64_t*       po = reinterpret_cast<uint64_t*>(out);
    for (int i = 0; i < PAYLOAD_BYTES / 8; ++i)
        po[i] = pa[i] ^ pb[i];
}

/* ── Stride-based pair mapping ─────────────────────────────────────────── */

/*
 * For a given frame seq and stride S, compute the partner's seq.
 * Partners form non-overlapping pairs within super-blocks of size 2S.
 *
 * Returns partner seq (always ≥ 0 for valid input).
 */
inline int compute_partner(uint16_t seq, uint8_t stride) {
    int block_size = 2 * stride;
    int offset     = seq % block_size;
    if (offset < stride)
        return seq + stride;   /* early frame → partner is later */
    else
        return seq - stride;   /* late frame → partner was earlier */
}

/*
 * Returns true if this seq is the "late" frame in its pair.
 * The sender should compute and emit parity when this returns true.
 */
inline bool is_parity_trigger(uint16_t seq, uint8_t stride) {
    return (seq % (2 * stride)) >= stride;
}

/*
 * Return the lower seq in the pair (the "base_seq" we embed in parity headers).
 */
inline uint16_t parity_base_seq(uint16_t seq, uint8_t stride) {
    int partner = compute_partner(seq, stride);
    return static_cast<uint16_t>(seq < partner ? seq : partner);
}

#endif /* FEC_HPP */
