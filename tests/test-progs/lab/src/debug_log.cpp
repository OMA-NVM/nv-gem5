#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace std;

// 與主程式相同的常數（為了固定大小的參數型別）
// 若未來要抽成共用 header 再統一即可
#define DESC_BYTES      64
#define PLANES          8
#define DIM             512
#define MAPS_PER_GROUP  8

// ------------------------------ Local helpers (internal linkage) ------------------------------
static size_t popcount_bytes(const uint8_t* p, size_t n) {
    size_t cnt = 0;
    size_t q = n / 8;
    for (size_t i = 0; i < q; ++i) {
        uint64_t w;
        memcpy(&w, p + i * 8, 8);
        cnt += __builtin_popcountll(w);
    }
    for (size_t i = q * 8; i < n; ++i) {
        cnt += __builtin_popcount(static_cast<unsigned>(p[i]));
    }
    return cnt;
}

static inline int byte_to_bit(uint8_t v) {
    if (v == static_cast<unsigned char>(0x00)) return 0;
    if (v == static_cast<unsigned char>(0xFF)) return 1;
    return (v ? 1 : 0);
}

static void dump_row_bits_to_log(ofstream& log,
                                 const char* tag,
                                 const uint8_t* row,
                                 bool group64 = true) {
    log << tag;
    for (size_t i = 0; i < DIM; ++i) {
        int bit = byte_to_bit(row[i]);
        log << bit;
        if (group64 && (i + 1) % 64 == 0 && (i + 1) < DIM) {
            log << " | ";
        } else if (i + 1 < DIM) {
            log << ' ';
        }
    }
    log << '\n';
}

static void dump_line_dec(ofstream& log, const char* tag, const uint8_t* data, size_t len) {
    log << tag;
    for (size_t i = 0; i < len; ++i) {
        log << static_cast<int>(data[i]);
        if (i + 1 < len) log << ' ';
    }
    log << '\n';
}

static void cpu_expected_bitwise(
    const uint8_t map_rows[PLANES][DIM],
    const uint8_t qry_rows[PLANES][DIM],
    uint8_t exp_xor[PLANES][DIM],
    uint8_t exp_xnor[PLANES][DIM],
    uint8_t exp_eqacc_after[PLANES][DIM],
    uint8_t exp_mapnz_after[PLANES][DIM],
    uint8_t exp_qrynz_after[PLANES][DIM],
    uint8_t exp_bothnz_final[DIM],
    uint8_t exp_match_final[DIM]
){
    uint8_t eq_acc[DIM];
    uint8_t map_nz[DIM];
    uint8_t qry_nz[DIM];
    for (size_t i = 0; i < DIM; ++i) {
        eq_acc[i] = static_cast<unsigned char>(0xFF);
        map_nz[i] = static_cast<unsigned char>(0x00);
        qry_nz[i] = static_cast<unsigned char>(0x00);
    }

    for (int b = 0; b < PLANES; ++b) {
        for (size_t i = 0; i < DIM; ++i) {
            exp_xor[b][i]  = static_cast<unsigned char>(map_rows[b][i] ^ qry_rows[b][i]);
            exp_xnor[b][i] = static_cast<unsigned char>(~exp_xor[b][i]);

            eq_acc[i] = static_cast<unsigned char>(eq_acc[i] & exp_xnor[b][i]);
            exp_eqacc_after[b][i] = eq_acc[i];

            map_nz[i] = static_cast<unsigned char>(map_nz[i] | map_rows[b][i]);
            exp_mapnz_after[b][i] = map_nz[i];

            qry_nz[i] = static_cast<unsigned char>(qry_nz[i] | qry_rows[b][i]);
            exp_qrynz_after[b][i] = qry_nz[i];
        }
    }

    for (size_t i = 0; i < DIM; ++i) {
        exp_bothnz_final[i] = static_cast<unsigned char>(map_nz[i] & qry_nz[i]);
        exp_match_final[i]  = static_cast<unsigned char>(eq_acc[i] & exp_bothnz_final[i]);
    }
}

// ------------------------------ Public logger ------------------------------
void write_debug_log_full(
    const uint8_t map8[MAPS_PER_GROUP][DESC_BYTES],
    const uint8_t query[DESC_BYTES],
    const uint8_t map_rows[PLANES][DIM],
    const uint8_t qry_rows[PLANES][DIM],
    const uint8_t act_rows_map[PLANES][DIM],
    const uint8_t act_rows_qry[PLANES][DIM],
    // actual per-plane
    const uint8_t act_xor[PLANES][DIM],
    const uint8_t act_xnor[PLANES][DIM],
    const uint8_t act_eqacc_after[PLANES][DIM],
    const uint8_t act_mapnz_after[PLANES][DIM],
    const uint8_t act_qrynz_after[PLANES][DIM],
    const uint8_t act_bothnz_final[DIM],
    const uint8_t act_match_final[DIM],
    int qry_base_row
){
    // 先以 CPU 模擬產生 expected
    uint8_t exp_xor[PLANES][DIM], exp_xnor[PLANES][DIM], exp_eqacc_after[PLANES][DIM],
            exp_mapnz_after[PLANES][DIM], exp_qrynz_after[PLANES][DIM],
            exp_bothnz_final[DIM], exp_match_final[DIM];
    memset(exp_xor, 0, sizeof(exp_xor));
    memset(exp_xnor, 0, sizeof(exp_xnor));
    memset(exp_eqacc_after, 0, sizeof(exp_eqacc_after));
    memset(exp_mapnz_after, 0, sizeof(exp_mapnz_after));
    memset(exp_qrynz_after, 0, sizeof(exp_qrynz_after));
    memset(exp_bothnz_final, 0, sizeof(exp_bothnz_final));
    memset(exp_match_final, 0, sizeof(exp_match_final));

    cpu_expected_bitwise(map_rows, qry_rows,
                         exp_xor, exp_xnor, exp_eqacc_after,
                         exp_mapnz_after, exp_qrynz_after,
                         exp_bothnz_final, exp_match_final);

    ofstream log("log.txt", ios::out | ios::trunc);
    if (!log) {
        fprintf(stderr, "Failed to open log.txt for writing.\n");
        return;
    }

    auto dump_row_pair_and_diff = [&](const char* head,
                                      const uint8_t* exp_row,
                                      const uint8_t* act_row) {
        log << head << " (bits)\n";
        dump_row_bits_to_log(log, "  EXP: ", exp_row, true);
        dump_row_bits_to_log(log, "  ACT: ", act_row, true);
        size_t diff_bits = 0;
        for (size_t i = 0; i < DIM; ++i) {
            if (byte_to_bit(exp_row[i]) != byte_to_bit(act_row[i])) diff_bits++;
        }
        log << "  DIFF: " << diff_bits << " bits differ\n";
    };

    // Raw 9 descriptors (8 maps + 1 query)
    log << "=== RAW DESCRIPTORS (decimal bytes) ===\n";
    for (int k = 0; k < MAPS_PER_GROUP; ++k) {
        char tag[32]; snprintf(tag, sizeof(tag), "MAP%d: ", k);
        dump_line_dec(log, tag, map8[k], DESC_BYTES);
    }
    dump_line_dec(log, "QUERY: ", query, DESC_BYTES);

    // Expected: rows (map planes & query planes)
    log << "\n=== EXPECTED CIM LAYOUT (bits; MAP planes then QUERY planes) ===\n";
    for (int b = 0; b < PLANES; ++b) {
        char tag[32]; snprintf(tag, sizeof(tag), "EXP MAP Rb%02d: ", b);
        dump_row_bits_to_log(log, tag, map_rows[b], true);
    }
    for (int b = 0; b < PLANES; ++b) {
        char tag[32]; snprintf(tag, sizeof(tag), "EXP QRY Rb%02d: ", b);
        dump_row_bits_to_log(log, tag, qry_rows[b], true);
    }

    // Actual readback
    log << "\n=== ACTUAL CIM ROWS READBACK (bits; group0 MAP rows & QUERY rows) ===\n";
    for (int b = 0; b < PLANES; ++b) {
        char tag[32]; snprintf(tag, sizeof(tag), "ACT MAP Rb%02d: ", b);
        dump_row_bits_to_log(log, tag, act_rows_map[b], true);
    }
    for (int b = 0; b < PLANES; ++b) {
        char tag[64]; snprintf(tag, sizeof(tag), "ACT QRY R%03d: ", qry_base_row + b);
        dump_row_bits_to_log(log, tag, act_rows_qry[b], true);
    }

    // Per-plane bitwise results
    log << "\n=== BITWISE PER-PLANE RESULTS (Expected vs Actual) ===\n";
    for (int b = 0; b < PLANES; ++b) {
        log << "\n-- Plane b=" << b << " --\n";
        dump_row_pair_and_diff("XOR (SCR_XOR snapshot)",     exp_xor[b],         act_xor[b]);
        dump_row_pair_and_diff("XNOR (SCR_XNOR snapshot)",   exp_xnor[b],        act_xnor[b]);
        dump_row_pair_and_diff("EQ_ACC after b (SCR_EQACC)", exp_eqacc_after[b], act_eqacc_after[b]);
        dump_row_pair_and_diff("MAP_NZ after b (SCR_MAPNZ)", exp_mapnz_after[b], act_mapnz_after[b]);
        dump_row_pair_and_diff("QRY_NZ after b (SCR_QRYNZ)", exp_qrynz_after[b], act_qrynz_after[b]);
    }

    // Presence and final match
    log << "\n=== PRESENCE & FINAL (Expected vs Actual) ===\n";
    dump_row_pair_and_diff("BOTH_NZ = MAP_NZ AND QRY_NZ (SCR_BOTHNZ)",
                           exp_bothnz_final, act_bothnz_final);
    dump_row_pair_and_diff("MATCH = EQ_ACC AND BOTH_NZ (SCR_MATCH)",
                           exp_match_final,  act_match_final);

    // Scores per 64-byte segment（8 段）
    log << "\n=== SCORES PER 64-BYTE SEGMENT (Expected vs Actual) ===\n";
    for (int seg = 0; seg < MAPS_PER_GROUP; ++seg) {
        const uint8_t* exp_seg = &exp_match_final[seg * DESC_BYTES];
        const uint8_t* act_seg = &act_match_final[seg * DESC_BYTES];
        size_t exp_bits = popcount_bytes(exp_seg, DESC_BYTES);
        size_t act_bits = popcount_bytes(act_seg, DESC_BYTES);
        size_t exp_dims = exp_bits / 8;
        size_t act_dims = act_bits / 8;

        log << "Segment " << seg << " (map" << seg << " vs query): "
            << "EXP bits=" << exp_bits << " (dims=" << exp_dims << "), "
            << "ACT bits=" << act_bits << " (dims=" << act_dims << ")\n";
    }

    log.flush();
    log.close();
}