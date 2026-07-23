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
#include <algorithm>

/* ── Frame tracking ────────────────────────────────────────────────────── */

static uint8_t  frame_payloads[MAX_FRAMES][PAYLOAD_BYTES];
static bool     frame_delivered[MAX_FRAMES];   /* true once forwarded to player */
static uint16_t highest_seq_received = 0;

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

static int player_fd_g;    /* set once in main() */
static int feedback_fd_g;  /* set once in main() */

static void deliver(uint16_t seq, const uint8_t* payload) {
    uint16_t idx = seq % MAX_FRAMES;
    if (frame_delivered[idx]) return;

    frame_delivered[idx] = true;
    std::memcpy(frame_payloads[idx], payload, PAYLOAD_BYTES);
    
    /* Proactively clear the future slot exactly halfway across the ring to prevent corruption */
    uint16_t clear_idx = (seq + (MAX_FRAMES / 2)) % MAX_FRAMES;
    frame_delivered[clear_idx] = false;

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

        uint16_t b_idx = base % MAX_FRAMES;
        uint16_t p_idx = partner % MAX_FRAMES;

        bool have_base    = frame_delivered[b_idx];
        bool have_partner = frame_delivered[p_idx];

        bool recovered = false;

        if (new_seq == base && have_base && !have_partner) {
            uint8_t rec[PAYLOAD_BYTES];
            xor_payloads(rec, sp.payload, frame_payloads[b_idx]);
            deliver(partner, rec);
            recovered = true;
        } else if (new_seq == partner && have_partner && !have_base) {
            uint8_t rec[PAYLOAD_BYTES];
            xor_payloads(rec, sp.payload, frame_payloads[p_idx]);
            deliver(base, rec);
            recovered = true;
        } else if (have_base && have_partner) {
            recovered = true;  /* both present → parity unneeded, remove */
        }

        /* Age out stale parities (older than 100 frames / 2 seconds) */
        bool stale = (uint16_t(new_seq - base) > 100 && uint16_t(new_seq - base) < 30000);

        if (recovered || stale) {
            /* Swap-remove: overwrite with last element */
            parity_store[i] = parity_store[--parity_count];
        } else {
            ++i;
        }
    }
}

/* ── Feedback generation ───────────────────────────────────────────────── */

static void evaluate_and_send_feedback() {
    static uint16_t last_eval_seq = 0;
    
    /* Need enough history to evaluate, leaving a 20-frame margin for jitter/delay */
    if (highest_seq_received < 30) return;
    
    uint16_t eval_end = highest_seq_received - 20;
    
    /* Evaluate every 25 frames */
    if (eval_end - last_eval_seq >= 25) {
        uint8_t max_burst = 0;
        uint8_t current_burst = 0;
        uint16_t loss_count = 0;
        uint16_t total = eval_end - last_eval_seq;
        
        for (uint16_t s = last_eval_seq; s != eval_end; ++s) {
            if (!frame_delivered[s % MAX_FRAMES]) {
                current_burst++;
                loss_count++;
                if (current_burst > max_burst) max_burst = current_burst;
            } else {
                current_burst = 0;
            }
        }
        
        uint8_t buf[FEEDBACK_SIZE];
        int len = encode_feedback(buf, max_burst, highest_seq_received, loss_count, total);
        send_to(feedback_fd_g, buf, len, PORT_RECV_TO_RELAY);
        
        last_eval_seq = eval_end;
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
    feedback_fd_g = feedback_fd;
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

                        /* Handle uint16_t sequence wrap-around */
                        if (uint16_t(seq - highest_seq_received) < 30000) {
                            highest_seq_received = seq;
                        }

                        /* Forward immediately — zero buffering */
                        deliver(seq, payload);

                        /* Check stored parities for deferred recovery */
                        try_deferred_recovery(seq);
                        
                        /* Periodically send feedback based on received sequence */
                        evaluate_and_send_feedback();

                    } else if (type == PKT_PARITY && n == PACKET_SIZE) {
                        uint16_t base_seq = parse_seq(recv_buf);
                        uint8_t  stride   = parse_stride(recv_buf);
                        const uint8_t* pp = parse_payload(recv_buf);
                        uint16_t partner  = base_seq + stride;

                        uint16_t b_idx = base_seq % MAX_FRAMES;
                        uint16_t p_idx = partner % MAX_FRAMES;

                        bool hb = frame_delivered[b_idx];
                        bool hp = frame_delivered[p_idx];

                        if (hb && !hp) {
                            /* Recover partner */
                            uint8_t rec[PAYLOAD_BYTES];
                            xor_payloads(rec, pp, frame_payloads[b_idx]);
                            deliver(partner, rec);
                        } else if (!hb && hp) {
                            /* Recover base */
                            uint8_t rec[PAYLOAD_BYTES];
                            xor_payloads(rec, pp, frame_payloads[p_idx]);
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
                n = recv_nb(relay_fd, recv_buf, sizeof(recv_buf));
            }
        }
    }

    return 0;
}
