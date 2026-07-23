/*
 * receiver.cpp — FEC-enabled receiver for The Flaky Network.
 *
 * Listens on port 47002 for DATA and PARITY packets from the relay.
 * DATA frames are forwarded to the harness player (port 47020) immediately
 * on arrival — no jitter buffer, no intentional holding.
 *
 * When a PARITY packet arrives:
 *   - If exactly one frame in its group is missing, recover it via XOR
 *     and forward immediately.
 *   - If both frames are missing, store the parity for deferred recovery
 *     (the missing frame may still arrive via the relay).
 *   - If both frames are present, discard (parity not needed).
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

/* ── Frame tracking ────────────────────────────────────────────────────── */

static uint8_t  frame_payloads[MAX_FRAMES][PAYLOAD_BYTES];
static bool     frame_delivered[MAX_FRAMES];   /* true once forwarded to player */

/* ── Stored parities for deferred recovery ─────────────────────────────── */

struct StoredParity {
    uint16_t base_seq;
    uint8_t  stride;
    uint8_t  payload[PAYLOAD_BYTES];
};

/* Fixed-size ring to avoid dynamic allocation.  750 parity packets max in
   a 30-second run; 1024 slots is more than enough. */
static constexpr int  MAX_STORED_PARITY = 1024;
static StoredParity   parity_store[MAX_STORED_PARITY];
static int            parity_count = 0;

/* ── Deliver a frame to the harness player ─────────────────────────────── */

static int player_fd_g;  /* set once in main() */

static void deliver(uint16_t seq, const uint8_t* payload) {
    if (seq >= MAX_FRAMES || frame_delivered[seq]) return;

    frame_delivered[seq] = true;
    std::memcpy(frame_payloads[seq], payload, PAYLOAD_BYTES);

    uint8_t buf[HARNESS_PACKET_SIZE];
    encode_harness(buf, static_cast<uint32_t>(seq), payload);
    send_to(player_fd_g, buf, HARNESS_PACKET_SIZE, PORT_RECV_TO_PLAYER);
}

/* ── Try to recover frames from stored parities ────────────────────────── */

static void try_deferred_recovery(uint16_t new_seq) {
    for (int i = 0; i < parity_count; ) {
        StoredParity& sp = parity_store[i];
        uint16_t base    = sp.base_seq;
        uint16_t partner = base + sp.stride;

        bool have_base    = (base    < MAX_FRAMES) && frame_delivered[base];
        bool have_partner = (partner < MAX_FRAMES) && frame_delivered[partner];

        bool recovered = false;

        if (new_seq == base && have_base && !have_partner && partner < MAX_FRAMES) {
            uint8_t rec[PAYLOAD_BYTES];
            xor_payloads(rec, sp.payload, frame_payloads[base]);
            deliver(partner, rec);
            recovered = true;
        } else if (new_seq == partner && have_partner && !have_base && base < MAX_FRAMES) {
            uint8_t rec[PAYLOAD_BYTES];
            xor_payloads(rec, sp.payload, frame_payloads[partner]);
            deliver(base, rec);
            recovered = true;
        } else if (have_base && have_partner) {
            recovered = true;  /* both present → parity unneeded, remove */
        }

        if (recovered) {
            /* Swap-remove: overwrite with last element */
            parity_store[i] = parity_store[--parity_count];
        } else {
            ++i;
        }
    }
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main() {
    /* Create sockets */
    int relay_fd    = create_udp_socket(PORT_RELAY_TO_RECV);  /* from relay  */
    int player_fd   = create_udp_socket();                     /* to player   */
    int feedback_fd = create_udp_socket();                     /* to relay    */

    if (relay_fd < 0 || player_fd < 0 || feedback_fd < 0) {
        std::fprintf(stderr, "receiver: socket init failed\n");
        return 1;
    }

    player_fd_g = player_fd;
    std::memset(frame_delivered, 0, sizeof(frame_delivered));

    uint8_t recv_buf[2048];

    /* ── Event loop ────────────────────────────────────────────────────── */
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(relay_fd, &rfds);
        int maxfd = relay_fd + 1;

        struct timeval tv = {0, 500};  /* 0.5 ms timeout — low latency */
        int ready = select(maxfd, &rfds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        if (FD_ISSET(relay_fd, &rfds)) {
            ssize_t n = recv_nb(relay_fd, recv_buf, sizeof(recv_buf));
            while (n > 0) {
                if (n >= HEADER_SIZE) {
                    uint8_t type = parse_type(recv_buf);

                    if (type == PKT_DATA && n == PACKET_SIZE) {
                        uint16_t seq = parse_seq(recv_buf);
                        const uint8_t* payload = parse_payload(recv_buf);

                        /* Forward immediately — zero buffering */
                        deliver(seq, payload);

                        /* Check stored parities for deferred recovery */
                        try_deferred_recovery(seq);

                    } else if (type == PKT_PARITY && n == PACKET_SIZE) {
                        uint16_t base_seq = parse_seq(recv_buf);
                        uint8_t  stride   = parse_stride(recv_buf);
                        const uint8_t* pp = parse_payload(recv_buf);
                        uint16_t partner  = base_seq + stride;

                        if (base_seq < MAX_FRAMES && partner < MAX_FRAMES) {
                            bool hb = frame_delivered[base_seq];
                            bool hp = frame_delivered[partner];

                            if (hb && !hp) {
                                /* Recover partner */
                                uint8_t rec[PAYLOAD_BYTES];
                                xor_payloads(rec, pp, frame_payloads[base_seq]);
                                deliver(partner, rec);
                            } else if (!hb && hp) {
                                /* Recover base */
                                uint8_t rec[PAYLOAD_BYTES];
                                xor_payloads(rec, pp, frame_payloads[partner]);
                                deliver(base_seq, rec);
                            } else if (!hb && !hp) {
                                /* Both missing — store for deferred recovery */
                                if (parity_count < MAX_STORED_PARITY) {
                                    StoredParity& sp = parity_store[parity_count++];
                                    sp.base_seq = base_seq;
                                    sp.stride   = stride;
                                    std::memcpy(sp.payload, pp, PAYLOAD_BYTES);
                                }
                            }
                            /* both present → discard parity silently */
                        }
                    }
                }
                n = recv_nb(relay_fd, recv_buf, sizeof(recv_buf));
            }
        }
    }

    return 0;
}
