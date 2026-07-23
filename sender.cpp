/*
 * sender.cpp — FEC-enabled sender for The Flaky Network.
 *
 * Receives 164-byte harness frames from port 47010, wraps each in our
 * wire format, computes XOR parity for K=2 stride-interleaved groups,
 * and sends both DATA and PARITY packets to the relay on port 47001.
 *
 * FEC strategy:
 *   - Every pair of frames (stride-interleaved) produces one parity packet.
 *   - Parity = XOR of the two frame payloads in the group.
 *   - Stride S means frame i pairs with frame i±S, so consecutive burst
 *     losses land in different groups and are independently recoverable.
 *   - With K=2 and S frames per super-block half, each lost frame can be
 *     recovered from its surviving partner + parity.
 *
 * Budget: 1500 data (164B) + 750 parity (164B) = 369,000B → 1.54× overhead.
 *
 * Build: make          Run: python3 run.py --delay_ms 60
 */

#include "protocol.hpp"
#include "fec.hpp"
#include "net.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/select.h>

/* ── Frame buffer ──────────────────────────────────────────────────────── */

static uint8_t  frame_payloads[MAX_FRAMES][PAYLOAD_BYTES];
static bool     frame_stored[MAX_FRAMES];

/* ── Main ──────────────────────────────────────────────────────────────── */

int main() {
    /* Configuration — stride 1 for minimal delay, adjustable via feedback */
    uint8_t stride = 1;

    /* Create sockets */
    int harness_fd  = create_udp_socket(PORT_SOURCE_TO_SENDER);  /* from harness  */
    int relay_fd    = create_udp_socket();                        /* to relay      */
    int feedback_fd = create_udp_socket(PORT_RELAY_TO_SEND);     /* from receiver */

    if (harness_fd < 0 || relay_fd < 0 || feedback_fd < 0) {
        std::fprintf(stderr, "sender: socket init failed\n");
        return 1;
    }

    std::memset(frame_stored, 0, sizeof(frame_stored));

    uint8_t recv_buf[2048];
    uint8_t send_buf[256];

    /* ── Event loop ────────────────────────────────────────────────────── */
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(harness_fd, &rfds);
        FD_SET(feedback_fd, &rfds);
        int maxfd = (harness_fd > feedback_fd ? harness_fd : feedback_fd) + 1;

        struct timeval tv = {0, 1000};  /* 1 ms timeout */
        int ready = select(maxfd, &rfds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        /* ── Harness frames ────────────────────────────────────────────── */
        if (FD_ISSET(harness_fd, &rfds)) {
            ssize_t n = recv_nb(harness_fd, recv_buf, sizeof(recv_buf));
            while (n > 0) {
                if (n == HARNESS_PACKET_SIZE) {
                    uint32_t seq32  = parse_harness_seq(recv_buf);
                    uint16_t seq    = static_cast<uint16_t>(seq32);
                    const uint8_t* payload = parse_harness_payload(recv_buf);

                    /* Store payload for parity computation */
                    if (seq < MAX_FRAMES) {
                        std::memcpy(frame_payloads[seq], payload, PAYLOAD_BYTES);
                        frame_stored[seq] = true;
                    }

                    /* Send DATA packet to relay */
                    int len = encode_data(send_buf, seq, stride, payload);
                    send_to(relay_fd, send_buf, len, PORT_SEND_TO_RELAY);

                    /* If this is the "late" frame in a pair, emit PARITY */
                    if (is_parity_trigger(seq, stride)) {
                        int partner = compute_partner(seq, stride);
                        if (partner >= 0 && partner < MAX_FRAMES
                                && frame_stored[partner]) {
                            uint8_t parity_payload[PAYLOAD_BYTES];
                            xor_payloads(parity_payload,
                                         frame_payloads[seq],
                                         frame_payloads[partner]);
                            uint16_t base = parity_base_seq(seq, stride);
                            int plen = encode_parity(send_buf, base, stride,
                                                     parity_payload);
                            send_to(relay_fd, send_buf, plen,
                                    PORT_SEND_TO_RELAY);
                        }
                    }
                }
                n = recv_nb(harness_fd, recv_buf, sizeof(recv_buf));
            }
        }

        /* ── Feedback from receiver (adaptive stride logic) ──────────── */
        if (FD_ISSET(feedback_fd, &rfds)) {
            ssize_t n;
            do { 
                n = recv_nb(feedback_fd, recv_buf, sizeof(recv_buf)); 
                if (n == FEEDBACK_SIZE && recv_buf[0] == PKT_FEEDBACK) {
                    uint8_t burst_len = fb_burst_len(recv_buf);
                    uint16_t loss_count = fb_loss_count(recv_buf);
                    
                    /* Adaptive stride selection */
                    if (burst_len >= 2) {
                        /* Burst losses detected -> increase protection (cap at 2 to stay under 120ms delay) */
                        stride = 2;
                    } else if (loss_count == 0) {
                        /* Clean network -> reduce delay */
                        stride = 1;
                    }
                }
            }
            while (n > 0);
        }
    }

    return 0;
}
