// desc_multiq_dualbank.cpp
#include "cim_api.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <iomanip>

using namespace std;

// ------------------------------ Config ------------------------------
// 256 rows × 64 cols (each row is 64 bytes = 512 bits)
// 8 sub-columns (8B each) when num_column_bits=6 and byteBits=3
#define ROWS        256
#define DIM         (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t)) // expect 64
#define DESC_BYTES  64    // descriptor: 64 dims × 8-bit
#define PLANES      8     // 8 bit-planes (bit-packed)

// Same layout per bank: 225 rows = 25 groups * 9 rows/group = 200 maps (8 maps per group)
#define MAP_ROWS_RESERVED   225
#define GROUP_ROWS          9
#define GROUPS              (MAP_ROWS_RESERVED / GROUP_ROWS) // 25
#define MAPS_PER_GROUP      8
#define MAPS_PER_BANK       (GROUPS * MAPS_PER_GROUP)        // 200

// Query planes + scratch (all < 256 so row index fits in uint8_t)
#define QRY_BASE_ROW        233       // rows 233..240 hold query planes
#define SCR_XOR_ROW         241
#define SCR_XNOR_ROW        242
#define SCR_EQACC_ROW       243
#define SCR_MAPNZ_ROW       244
#define SCR_QRYNZ_ROW       245
#define SCR_BOTHNZ_ROW      246
#define SCR_MATCH_ROW       247

// Number of queries processed sequentially
#define NUM_QUERIES         8

// Bit-packed plane geometry: 64 dims -> 64 bits -> 8 bytes per plane segment
#define PLANE_BITS          64
#define PLANE_BYTES         (PLANE_BITS / 8)  // 8

// Dual-bank config
static constexpr int      NUM_BANK_BITS = 1;   // 2 banks
static constexpr uint8_t  BANK0 = 0;
static constexpr uint8_t  BANK1 = 1;
static constexpr uint32_t MASK_BANK0 = (1u << BANK0);
static constexpr uint32_t MASK_BANK1 = (1u << BANK1);
static constexpr uint32_t MASK_BANK_BOTH = (MASK_BANK0 | MASK_BANK1);
static constexpr uint64_t COLMASK_ALL = 0xFFull; // 8 sub-columns

static_assert(ROWS >= 248, "Need at least rows up to 247 for query/scratch.");
static_assert(DIM  == 64,  "Expect 64 bytes per row (num_column_bits = 6).");
static_assert(MAPS_PER_GROUP * PLANE_BYTES == DIM,
              "8 maps × 8B per plane must fill one row (64B).");
static_assert(MAP_ROWS_RESERVED % GROUP_ROWS == 0, "MAP rows must be multiple of 9.");

// ------------------------------ Helpers ------------------------------
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

// Convert a 64-byte descriptor into 8 bit-planes, bit-packed.
// planes[b] has 8 bytes; bit (d%8) of planes[b][d/8] stores ((desc[d] >> b) & 1).
static void to_bitplanes64(const uint8_t desc[DESC_BYTES], uint8_t planes[PLANES][PLANE_BYTES]) {
    memset(planes, 0, PLANES * PLANE_BYTES);
    for (int d = 0; d < 64; ++d) {
        int byte_idx = d >> 3;   // d / 8
        int bit_pos  = d & 7;    // d % 8 (LSB-first)
        uint8_t mask = static_cast<uint8_t>(1u << bit_pos);
        uint8_t v    = desc[d];
        for (int b = 0; b < PLANES; ++b) {
            if ((v >> b) & 1u) planes[b][byte_idx] |= mask;
        }
    }
}

// Build 8 MAP rows (each 64B) by horizontally concatenating 8 × 8B plane segments (bit-packed).
static void build_map_rows(uint8_t rows_out[PLANES][DIM], const uint8_t map8[MAPS_PER_GROUP][DESC_BYTES]) {
    uint8_t bp[PLANES][PLANE_BYTES];
    memset(rows_out, 0, PLANES * DIM);
    for (int k = 0; k < MAPS_PER_GROUP; ++k) {
        to_bitplanes64(map8[k], bp);
        for (int b = 0; b < PLANES; ++b) {
            memcpy(&rows_out[b][k * PLANE_BYTES], bp[b], PLANE_BYTES);
        }
    }
}

// Build 8 QUERY rows (each 64B) by replicating the same 8B plane 8 times.
static void build_query_rows(uint8_t rows_out[PLANES][DIM], const uint8_t query[DESC_BYTES]) {
    uint8_t bp[PLANES][PLANE_BYTES];
    to_bitplanes64(query, bp);
    for (int b = 0; b < PLANES; ++b) {
        for (int k = 0; k < MAPS_PER_GROUP; ++k) {
            memcpy(&rows_out[b][k * PLANE_BYTES], bp[b], PLANE_BYTES);
        }
    }
}

// Group row address helpers
static inline int row_id_of_group(int g)           { return 9*g + 0; }
static inline int row_plane_of_group(int g, int b) { return 9*g + 1 + b; }

// Pack 8×64-bit IDs into one 64-byte row: each ID is 8B, little-endian, placed in segment s.
static void pack_id_row_8x64(const uint64_t ids[MAPS_PER_GROUP], uint8_t out_row[DIM]) {
    memset(out_row, 0, DIM);
    for (int s = 0; s < MAPS_PER_GROUP; ++s) {
        uint8_t* p = &out_row[s * PLANE_BYTES];
        uint64_t v = ids[s];
        p[0] = static_cast<uint8_t>( v        & 0xFF);
        p[1] = static_cast<uint8_t>((v >>  8) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
        p[4] = static_cast<uint8_t>((v >> 32) & 0xFF);
        p[5] = static_cast<uint8_t>((v >> 40) & 0xFF);
        p[6] = static_cast<uint8_t>((v >> 48) & 0xFF);
        p[7] = static_cast<uint8_t>((v >> 56) & 0xFF);
    }
}

// Unpack one 64-bit ID from a 64-byte ID row segment (seg 0..7), little-endian.
static uint64_t unpack_id_from_row_seg(const uint8_t id_row[DIM], int seg) {
    const uint8_t* p = &id_row[seg * PLANE_BYTES];
    uint64_t v = 0;
    v |= static_cast<uint64_t>(p[0]);
    v |= static_cast<uint64_t>(p[1]) << 8;
    v |= static_cast<uint64_t>(p[2]) << 16;
    v |= static_cast<uint64_t>(p[3]) << 24;
    v |= static_cast<uint64_t>(p[4]) << 32;
    v |= static_cast<uint64_t>(p[5]) << 40;
    v |= static_cast<uint64_t>(p[6]) << 48;
    v |= static_cast<uint64_t>(p[7]) << 56;
    return v;
}

// ------------------------------ CPU expected (for one group vs one query) ------------------------------
// Input rows are bit-packed; XOR/XNOR/AND/OR are done bytewise but apply bitwise to packed bits.
static void cpu_expected_match_row(
    const uint8_t map_rows[PLANES][DIM],
    const uint8_t qry_rows[PLANES][DIM],
    uint8_t exp_match_final[DIM]
){
    uint8_t eq_acc[DIM];
    uint8_t map_nz[DIM];
    uint8_t qry_nz[DIM];
    for (size_t i = 0; i < DIM; ++i) {
        eq_acc[i] = 0xFF;
        map_nz[i] = 0x00;
        qry_nz[i] = 0x00;
    }

    for (int b = 0; b < PLANES; ++b) {
        for (size_t i = 0; i < DIM; ++i) {
            uint8_t x    = static_cast<uint8_t>(map_rows[b][i] ^ qry_rows[b][i]);
            uint8_t xnor = static_cast<uint8_t>(~x);
            eq_acc[i] = static_cast<uint8_t>(eq_acc[i] & xnor);
            map_nz[i] = static_cast<uint8_t>(map_nz[i] | map_rows[b][i]);
            qry_nz[i] = static_cast<uint8_t>(qry_nz[i] | qry_rows[b][i]);
        }
    }

    for (size_t i = 0; i < DIM; ++i) {
        exp_match_final[i] = static_cast<uint8_t>(eq_acc[i] & (map_nz[i] & qry_nz[i]));
    }
}

// ------------------------------ CIM per-group compute (dual-bank) ------------------------------
static void cim_match_group_presence_banked(
    CimModule& cim, int g, uint32_t bankmask
){
    const uint8_t  bytemask = 0xFF;
    const uint64_t colmask  = COLMASK_ALL;

    // Reset scratch rows in both banks explicitly (write from host)
    uint8_t zeros[DIM], ones[DIM];
    memset(zeros, 0x00, DIM);
    memset(ones,  0xFF, DIM);
    for (uint8_t bk : {BANK0, BANK1}) {
        cim.copy_to_cim(bk, SCR_EQACC_ROW,  ones,  NUM_BANK_BITS, DIM);
        cim.copy_to_cim(bk, SCR_MAPNZ_ROW,  zeros, NUM_BANK_BITS, DIM);
        cim.copy_to_cim(bk, SCR_QRYNZ_ROW,  zeros, NUM_BANK_BITS, DIM);
    }

    for (int b = 0; b < PLANES; ++b) {
        uint8_t map_row_b = static_cast<uint8_t>(row_plane_of_group(g, b));
        uint8_t qry_row_b = static_cast<uint8_t>(QRY_BASE_ROW + b);

        // XOR -> TEMP(SCR_XOR_ROW)
        cim.XOR({map_row_b, qry_row_b}, bytemask, bankmask, colmask, SCR_XOR_ROW);
        // materialize TEMP -> RW for SCR_XOR_ROW
        cim.COPY(static_cast<uint16_t>(SCR_XOR_ROW),
                 static_cast<uint16_t>(0x100u | SCR_XOR_ROW),
                 0, bytemask, bankmask, colmask);

        // XNOR = NOT(XOR)
        cim.NOT_COND(static_cast<uint16_t>(SCR_XNOR_ROW),
                     static_cast<uint16_t>(SCR_XOR_ROW),
                     true, false, bytemask, bankmask, colmask);

        // EQ_ACC = EQ_ACC & XNOR
        cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_XNOR_ROW)},
                bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_EQACC_ROW),
                 static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
                 0, bytemask, bankmask, colmask);

        // MAP_NZ |= MAP_plane
        cim.OR({static_cast<uint8_t>(SCR_MAPNZ_ROW), map_row_b},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_MAPNZ_ROW),
                 static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
                 0, bytemask, bankmask, colmask);

        // QRY_NZ |= QRY_plane
        cim.OR({static_cast<uint8_t>(SCR_QRYNZ_ROW), qry_row_b},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_QRYNZ_ROW),
                 static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
                 0, bytemask, bankmask, colmask);
    }

    // BOTH_NZ = MAP_NZ & QRY_NZ
    cim.AND({static_cast<uint8_t>(SCR_MAPNZ_ROW), static_cast<uint8_t>(SCR_QRYNZ_ROW)},
            bytemask, bankmask, colmask, SCR_BOTHNZ_ROW);
    cim.COPY(static_cast<uint16_t>(SCR_BOTHNZ_ROW),
             static_cast<uint16_t>(0x100u | SCR_BOTHNZ_ROW),
             0, bytemask, bankmask, colmask);

    // MATCH = EQ_ACC & BOTH_NZ
    cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_BOTHNZ_ROW)},
            bytemask, bankmask, colmask, SCR_MATCH_ROW);
    cim.COPY(static_cast<uint16_t>(SCR_MATCH_ROW),
             static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
             0, bytemask, bankmask, colmask);
}

// ------------------------------ Types ------------------------------
struct MapScore {
    uint64_t map_id;
    uint64_t query_id;
    int      bank;         // 0 or 1
    int      group_idx;
    int      seg_idx;      // 0..7
    size_t   bits_equal;   // 0..64 (bit-packed)
    size_t   dims_equal;   // 0..64 (== bits_equal)
};

struct Mismatch {
    int      bank;         // 0 or 1
    int      group_idx;
    int      seg_idx;
    uint64_t map_id;
    size_t   hw_bits, hw_dims;
    size_t   exp_bits, exp_dims;
};

// ------------------------------ Generators ------------------------------
static inline uint64_t make_map_id(int bank, int m) {
    // Different base per bank, stable per m
    uint64_t base = (bank == 0) ? 0xABCDEF0000000000ull : 0xABCDEF1000000000ull;
    return base ^ static_cast<uint64_t>(m * 0x9E3779B97F4A7C15ull);
}

static void gen_descriptor_for_map(int bank, int m, uint8_t out_desc[DESC_BYTES]) {
    // Slightly different per bank to ensure data diversity
    uint8_t salt = static_cast<uint8_t>(bank ? 0x33 : 0x11);
    for (int i = 0; i < DESC_BYTES; ++i) {
        out_desc[i] = static_cast<unsigned char>((i * 3 + 7 + m * 5 + salt) & 0xFF);
    }
}

// ------------------------------ Main ------------------------------
int main() {
    printf("Array: %dx%d (rows x cols), ROW_BYTES=%d, DESC_BYTES=%d, PLANES=%d, banks=%d\n",
           ROWS, DIM, (int)DIM, DESC_BYTES, PLANES, 1 << NUM_BANK_BITS);
    printf("Groups=%d, Maps/group=%d, Maps/bank=%d, Total maps=%d\n",
           GROUPS, MAPS_PER_GROUP, MAPS_PER_BANK, MAPS_PER_BANK * 2);

    CimModule cim;

    // Prepare maps per bank (IDs + descriptors)
    vector<uint64_t> all_ids[2];
    vector< array<uint8_t, DESC_BYTES> > all_desc[2];
    all_ids[0].resize(MAPS_PER_BANK);
    all_ids[1].resize(MAPS_PER_BANK);
    all_desc[0].resize(MAPS_PER_BANK);
    all_desc[1].resize(MAPS_PER_BANK);

    for (int bank = 0; bank < 2; ++bank) {
        for (int m = 0; m < MAPS_PER_BANK; ++m) {
            all_ids[bank][m] = make_map_id(bank, m);
            gen_descriptor_for_map(bank, m, all_desc[bank][m].data());
        }
    }

    // Clear both banks
    uint8_t zeros[DIM]; memset(zeros, 0x00, DIM);
    for (int bank = 0; bank < 2; ++bank) {
        for (int r = 0; r < ROWS; ++r) cim.copy_to_cim(static_cast<uint8_t>(bank), r, zeros, NUM_BANK_BITS, DIM);
    }

    // Load all groups into both banks (ID row + 8 plane rows)
    uint8_t id_row_buf[DIM];
    uint8_t map_rows_buf[PLANES][DIM];

    for (int g = 0; g < GROUPS; ++g) {
        for (int bank = 0; bank < 2; ++bank) {
            uint8_t map8[MAPS_PER_GROUP][DESC_BYTES];
            uint64_t ids8[MAPS_PER_GROUP];
            for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                int m = g * MAPS_PER_GROUP + s;
                ids8[s] = all_ids[bank][m];
                memcpy(map8[s], all_desc[bank][m].data(), DESC_BYTES);
            }
            // ID row
            pack_id_row_8x64(ids8, id_row_buf);
            cim.copy_to_cim(static_cast<uint8_t>(bank),
                            row_id_of_group(g), id_row_buf, NUM_BANK_BITS, DIM);

            // 8 plane rows
            build_map_rows(map_rows_buf, map8);
            for (int b = 0; b < PLANES; ++b) {
                cim.copy_to_cim(static_cast<uint8_t>(bank),
                                row_plane_of_group(g, b), (void*)map_rows_buf[b],
                                NUM_BANK_BITS, DIM);
            }
        }
    }

    // Build NUM_QUERIES queries (deterministic tweaks based on original scheme)
    int q_indices[NUM_QUERIES] = {37, 5, 80, 123, 150, 0, 199, 88};

    // Open log
    ofstream log("log.txt", ios::out | ios::trunc);
    if (!log) {
        fprintf(stderr, "Failed to open log.txt for writing.\n");
        return 1;
    }

    const uint8_t bytemask = 0xFF;
    const uint64_t colmask = COLMASK_ALL;

    for (int qi = 0; qi < NUM_QUERIES; ++qi) {
        const int q_src_idx = q_indices[qi];
        uint64_t query_id = 0x1234567800000000ull | static_cast<uint64_t>(q_src_idx);

        // Choose one source descriptor set for query (e.g., from bank0 pool)
        uint8_t query[DESC_BYTES];
        memcpy(query, all_desc[0][q_src_idx].data(), DESC_BYTES);

        // Deterministic tweaks
        query[0] = static_cast<unsigned char>(query[0] ^ (qi * 0x11));
        for (int i = 10; i <= 15; ++i) {
            query[i] = static_cast<unsigned char>(query[i] ^ (0x0F ^ (qi & 0x3)));
        }

        // Build query rows (bit-packed)
        uint8_t qry_rows[PLANES][DIM]; memset(qry_rows, 0, sizeof(qry_rows));
        build_query_rows(qry_rows, query);

        // Write query planes to both banks
        for (int b = 0; b < PLANES; ++b) {
            cim.copy_to_cim(BANK0, QRY_BASE_ROW + b, (void*)qry_rows[b], NUM_BANK_BITS, DIM);
            cim.copy_to_cim(BANK1, QRY_BASE_ROW + b, (void*)qry_rows[b], NUM_BANK_BITS, DIM);
        }

        // Mismatch collectors per bank
        vector<Mismatch> mismatches_b0;
        vector<Mismatch> mismatches_b1;
        mismatches_b0.reserve(16);
        mismatches_b1.reserve(16);

        for (int g = 0; g < GROUPS; ++g) {
            // CPU expected per bank (bank0)
            uint8_t map8_b0[MAPS_PER_GROUP][DESC_BYTES];
            for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                int m = g * MAPS_PER_GROUP + s;
                memcpy(map8_b0[s], all_desc[0][m].data(), DESC_BYTES);
            }
            uint8_t map_rows_cpu_b0[PLANES][DIM];
            build_map_rows(map_rows_cpu_b0, map8_b0);

            uint8_t exp_match_row_b0[DIM]; memset(exp_match_row_b0, 0, sizeof(exp_match_row_b0));
            cpu_expected_match_row(map_rows_cpu_b0, qry_rows, exp_match_row_b0);

            // CPU expected per bank (bank1)
            uint8_t map8_b1[MAPS_PER_GROUP][DESC_BYTES];
            for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                int m = g * MAPS_PER_GROUP + s;
                memcpy(map8_b1[s], all_desc[1][m].data(), DESC_BYTES);
            }
            uint8_t map_rows_cpu_b1[PLANES][DIM];
            build_map_rows(map_rows_cpu_b1, map8_b1);

            uint8_t exp_match_row_b1[DIM]; memset(exp_match_row_b1, 0, sizeof(exp_match_row_b1));
            cpu_expected_match_row(map_rows_cpu_b1, qry_rows, exp_match_row_b1);

            // CIM compute on both banks at once
            cim_match_group_presence_banked(cim, g, MASK_BANK_BOTH);

            // Read back MATCH row and ID row from each bank
            uint8_t match_row_b0[DIM], match_row_b1[DIM];
            uint8_t id_row_b0[DIM],    id_row_b1[DIM];
            memset(match_row_b0, 0, DIM); memset(id_row_b0, 0, DIM);
            memset(match_row_b1, 0, DIM); memset(id_row_b1, 0, DIM);

            cim.copy_to_cpu(match_row_b0, BANK0, SCR_MATCH_ROW, NUM_BANK_BITS, DIM);
            cim.copy_to_cpu(id_row_b0,    BANK0, row_id_of_group(g), NUM_BANK_BITS, DIM);

            cim.copy_to_cpu(match_row_b1, BANK1, SCR_MATCH_ROW, NUM_BANK_BITS, DIM);
            cim.copy_to_cpu(id_row_b1,    BANK1, row_id_of_group(g), NUM_BANK_BITS, DIM);

            // Compare per 8-byte segment (8 segments)
            for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                // bank0
                {
                    const uint8_t* hw_seg  = &match_row_b0[s * PLANE_BYTES];
                    const uint8_t* exp_seg = &exp_match_row_b0[s * PLANE_BYTES];
                    size_t hw_bits  = popcount_bytes(hw_seg,  PLANE_BYTES);
                    size_t exp_bits = popcount_bytes(exp_seg, PLANE_BYTES);
                    if (hw_bits != exp_bits) {
                        Mismatch mm{};
                        mm.bank      = 0;
                        mm.group_idx = g;
                        mm.seg_idx   = s;
                        mm.map_id    = unpack_id_from_row_seg(id_row_b0, s);
                        mm.hw_bits   = hw_bits;
                        mm.exp_bits  = exp_bits;
                        mm.hw_dims   = hw_bits;
                        mm.exp_dims  = exp_bits;
                        if (mismatches_b0.size() < 50) mismatches_b0.push_back(mm);
                    }
                }
                // bank1
                {
                    const uint8_t* hw_seg  = &match_row_b1[s * PLANE_BYTES];
                    const uint8_t* exp_seg = &exp_match_row_b1[s * PLANE_BYTES];
                    size_t hw_bits  = popcount_bytes(hw_seg,  PLANE_BYTES);
                    size_t exp_bits = popcount_bytes(exp_seg, PLANE_BYTES);
                    if (hw_bits != exp_bits) {
                        Mismatch mm{};
                        mm.bank      = 1;
                        mm.group_idx = g;
                        mm.seg_idx   = s;
                        mm.map_id    = unpack_id_from_row_seg(id_row_b1, s);
                        mm.hw_bits   = hw_bits;
                        mm.exp_bits  = exp_bits;
                        mm.hw_dims   = hw_bits;
                        mm.exp_dims  = exp_bits;
                        if (mismatches_b1.size() < 50) mismatches_b1.push_back(mm);
                    }
                }
            }
        }

        // Write per-bank results to log.txt
        auto dump_bank_result = [&](int bank, const vector<Mismatch>& mismatches) {
            if (mismatches.empty()) {
                log << "[Query " << qi << "][Bank " << bank << "] ID=0x"
                    << hex << setw(16) << setfill('0') << (unsigned long long)query_id << dec
                    << " : OK (all " << MAPS_PER_BANK << " maps match expected scores)\n";
            } else {
                log << "[Query " << qi << "][Bank " << bank << "] ID=0x"
                    << hex << setw(16) << setfill('0') << (unsigned long long)query_id << dec
                    << " : MISMATCH count=" << (int)mismatches.size() << " (showing up to 50)\n";
                for (const auto& mm : mismatches) {
                    log << "  g=" << setw(2) << setfill('0') << mm.group_idx
                        << " seg=" << mm.seg_idx
                        << " MapID=0x" << hex << setw(16) << setfill('0')
                        << (unsigned long long)mm.map_id << dec
                        << "  HW: bits=" << mm.hw_bits << " dims=" << mm.hw_dims
                        << "  EXP: bits=" << mm.exp_bits << " dims=" << mm.exp_dims
                        << "\n";
                }
            }
        };

        dump_bank_result(0, mismatches_b0);
        dump_bank_result(1, mismatches_b1);
    }

    log.flush();
    log.close();

    printf("All %d queries processed across 2 banks. Summary written to log.txt\n", NUM_QUERIES);
    return 0;
}