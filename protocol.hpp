/*
 * protocol.hpp — Wire format shared between sender and receiver.
 *
 * Packet layout (DATA / PARITY):
 *   [0]     type       (uint8:  0x01=DATA, 0x02=PARITY, 0x03=FEEDBACK)
 *   [1]     stride     (uint8:  interleaving stride used for FEC grouping)
 *   [2..3]  seq        (uint16 BE: frame seq for DATA, base_seq for PARITY)
 *   [4..163] payload   (160 bytes: raw frame or XOR parity)
 *
 * Total: 164 bytes — same size as the harness format, keeps overhead minimal.
 *
 * FEEDBACK packet (receiver → relay → sender):
 *   [0]     type       (0x03)
 *   [1]     burst_len  (uint8: max consecutive loss run observed)
 *   [2..3]  highest_rx (uint16 BE: highest contiguous seq received)
 *   [4..5]  loss_count (uint16 BE: losses in observation window)
 *   [6..7]  total_cnt  (uint16 BE: total frames in observation window)
 *   Total: 8 bytes
 */

#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

static constexpr int  PAYLOAD_BYTES      = 160;
static constexpr int  FRAME_MS           = 20;
static constexpr int  MAX_FRAMES         = 4096;

/* Packet types */
static constexpr uint8_t PKT_DATA     = 0x01;
static constexpr uint8_t PKT_PARITY   = 0x02;
static constexpr uint8_t PKT_FEEDBACK = 0x03;

/* Header / packet sizes */
static constexpr int HEADER_SIZE         = 4;
static constexpr int PACKET_SIZE         = HEADER_SIZE + PAYLOAD_BYTES;  /* 164 */

static constexpr int HARNESS_HEADER_SIZE = 4;   /* uint32 BE seq */
static constexpr int HARNESS_PACKET_SIZE = HARNESS_HEADER_SIZE + PAYLOAD_BYTES;

static constexpr int FEEDBACK_SIZE       = 8;

/* UDP ports (all on 127.0.0.1) */
static constexpr int PORT_SOURCE_TO_SENDER = 47010;
static constexpr int PORT_SEND_TO_RELAY    = 47001;
static constexpr int PORT_RELAY_TO_RECV    = 47002;
static constexpr int PORT_RECV_TO_RELAY    = 47003;
static constexpr int PORT_RELAY_TO_SEND    = 47004;
static constexpr int PORT_RECV_TO_PLAYER   = 47020;

/* ── Encode helpers ────────────────────────────────────────────────────── */

/* Encode a DATA packet: type(1) + stride(1) + seq(2) + payload(160) = 164 */
inline int encode_data(uint8_t* buf, uint16_t seq, uint8_t stride,
                       const uint8_t* payload) {
    buf[0] = PKT_DATA;
    buf[1] = stride;
    uint16_t ns = htons(seq);
    std::memcpy(buf + 2, &ns, 2);
    std::memcpy(buf + HEADER_SIZE, payload, PAYLOAD_BYTES);
    return PACKET_SIZE;
}

/* Encode a PARITY packet: type(1) + stride(1) + base_seq(2) + parity(160) */
inline int encode_parity(uint8_t* buf, uint16_t base_seq, uint8_t stride,
                         const uint8_t* parity_payload) {
    buf[0] = PKT_PARITY;
    buf[1] = stride;
    uint16_t ns = htons(base_seq);
    std::memcpy(buf + 2, &ns, 2);
    std::memcpy(buf + HEADER_SIZE, parity_payload, PAYLOAD_BYTES);
    return PACKET_SIZE;
}

/* Encode a FEEDBACK packet (8 bytes) */
inline int encode_feedback(uint8_t* buf, uint8_t burst_len,
                           uint16_t highest_rx, uint16_t loss_count,
                           uint16_t total_count) {
    buf[0] = PKT_FEEDBACK;
    buf[1] = burst_len;
    uint16_t ns;
    ns = htons(highest_rx);   std::memcpy(buf + 2, &ns, 2);
    ns = htons(loss_count);   std::memcpy(buf + 4, &ns, 2);
    ns = htons(total_count);  std::memcpy(buf + 6, &ns, 2);
    return FEEDBACK_SIZE;
}

/* Encode harness format: 4-byte BE uint32 seq + 160-byte payload */
inline int encode_harness(uint8_t* buf, uint32_t seq,
                          const uint8_t* payload) {
    uint32_t nl = htonl(seq);
    std::memcpy(buf, &nl, 4);
    std::memcpy(buf + 4, payload, PAYLOAD_BYTES);
    return HARNESS_PACKET_SIZE;
}

/* ── Decode helpers ────────────────────────────────────────────────────── */

inline uint8_t  parse_type   (const uint8_t* buf) { return buf[0]; }
inline uint8_t  parse_stride (const uint8_t* buf) { return buf[1]; }

inline uint16_t parse_seq(const uint8_t* buf) {
    uint16_t ns;
    std::memcpy(&ns, buf + 2, 2);
    return ntohs(ns);
}

inline const uint8_t* parse_payload(const uint8_t* buf) {
    return buf + HEADER_SIZE;
}

/* Harness-format decoders */
inline uint32_t parse_harness_seq(const uint8_t* buf) {
    uint32_t nl;
    std::memcpy(&nl, buf, 4);
    return ntohl(nl);
}

inline const uint8_t* parse_harness_payload(const uint8_t* buf) {
    return buf + HARNESS_HEADER_SIZE;
}

/* Feedback decoders */
inline uint8_t  fb_burst_len  (const uint8_t* b) { return b[1]; }
inline uint16_t fb_highest_rx (const uint8_t* b) { uint16_t v; std::memcpy(&v,b+2,2); return ntohs(v); }
inline uint16_t fb_loss_count (const uint8_t* b) { uint16_t v; std::memcpy(&v,b+4,2); return ntohs(v); }
inline uint16_t fb_total_count(const uint8_t* b) { uint16_t v; std::memcpy(&v,b+6,2); return ntohs(v); }

#endif /* PROTOCOL_HPP */
