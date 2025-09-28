// desc_multiq_nomemdebug.cpp
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
// 256 rows × 512 cols (each row is 512 bytes)
#define ROWS        256
#define DIM         (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t)) // expected 512
#define DESC_BYTES  64
#define PLANES      8

// Map area: 225 rows = 25 groups * 9 rows/group = 200 maps (8 maps per group)
#define MAP_ROWS_RESERVED   225
#define GROUP_ROWS          9
#define GROUPS              (MAP_ROWS_RESERVED / GROUP_ROWS) // 25
#define MAPS_PER_GROUP      8
#define MAPS_TOTAL          (GROUPS * MAPS_PER_GROUP)        // 200

// Query planes + scratch (fixed at high rows, all < 256 to fit uint8_t row indices)
// rows 233..240 hold the 8 bit-planes of the current query (each plane replicated 8× 64B)
// rows 241..247 are scratch
#define QRY_BASE_ROW        233       // rows 233..240
#define SCR_XOR_ROW         241
#define SCR_XNOR_ROW        242
#define SCR_EQACC_ROW       243
#define SCR_MAPNZ_ROW       244
#define SCR_QRYNZ_ROW       245
#define SCR_BOTHNZ_ROW      246
#define SCR_MATCH_ROW       247

// This version processes 8 queries sequentially (load one query, sweep all maps, then next)
#define NUM_QUERIES         8

static_assert(ROWS >= 248, "Need at least rows up to 247 for query/scratch.");
static_assert(DIM  == 512, "This test expects 512 columns per row.");
static_assert(DESC_BYTES * 8 == DIM, "8 descriptors of 64B must fill one row.");
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

// Convert a 64-byte descriptor into 8 bit-planes (each plane length 64, values 0x00/0xFF).
static void to_bitplanes64(const uint8_t desc[DESC_BYTES], uint8_t planes[PLANES][DESC_BYTES]) {
    for (int b = 0; b < PLANES; ++b) {
        for (int i = 0; i < DESC_BYTES; ++i) {
            planes[b][i] = static_cast<unsigned char>(((desc[i] >> b) & 1) ? 0xFF : 0x00);
        }
    }
}

// Build 8 MAP rows (each 512B) by horizontally concatenating 8 × 64B plane segments.
static void build_map_rows(uint8_t rows_out[PLANES][DIM], const uint8_t map8[MAPS_PER_GROUP][DESC_BYTES]) {
    uint8_t bp[PLANES][DESC_BYTES];
    for (int k = 0; k < MAPS_PER_GROUP; ++k) {
        to_bitplanes64(map8[k], bp);
        for (int b = 0; b < PLANES; ++b) {
            memcpy(&rows_out[b][k * DESC_BYTES], bp[b], DESC_BYTES);
        }
    }
}

// Build 8 QUERY rows (each 512B) by replicating the same 64B plane 8 times.
static void build_query_rows(uint8_t rows_out[PLANES][DIM], const uint8_t query[DESC_BYTES]) {
    uint8_t bp[PLANES][DESC_BYTES];
    to_bitplanes64(query, bp);
    for (int b = 0; b < PLANES; ++b) {
        for (int k = 0; k < MAPS_PER_GROUP; ++k) {
            memcpy(&rows_out[b][k * DESC_BYTES], bp[b], DESC_BYTES);
        }
    }
}

// Group row address helpers
static inline int row_id_of_group(int g)           { return 9*g + 0; }
static inline int row_plane_of_group(int g, int b) { return 9*g + 1 + b; }

// Pack 8×64-bit IDs into one 512-column row (each ID is 64 columns, LSB→MSB in column order)
static void pack_id_row_8x64(const uint64_t ids[MAPS_PER_GROUP], uint8_t out_row[DIM]) {
    memset(out_row, 0, DIM);
    for (int s = 0; s < MAPS_PER_GROUP; ++s) {
        for (int i = 0; i < 64; ++i) {
            int bit = ( (ids[s] >> i) & 1ull ) ? 1 : 0; // LSB first
            out_row[s * 64 + i] = static_cast<unsigned char>(bit ? 0xFF : 0x00);
        }
    }
}

// Unpack one 64-bit ID from a 512-column ID row segment (seg 0..7)
static uint64_t unpack_id_from_row_seg(const uint8_t id_row[DIM], int seg /*0..7*/) {
    uint64_t id = 0;
    const uint8_t* p = &id_row[seg * 64];
    for (int i = 0; i < 64; ++i) {
        if (p[i]) id |= (1ull << i); // bytes are 0x00/0xFF; non-zero means bit=1
    }
    return id;
}

// ------------------------------ CPU expected (for one group vs one query) ------------------------------
static void cpu_expected_match_row(
    const uint8_t map_rows[PLANES][DIM],
    const uint8_t qry_rows[PLANES][DIM],
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
            uint8_t x = static_cast<unsigned char>(map_rows[b][i] ^ qry_rows[b][i]);
            uint8_t xnor = static_cast<unsigned char>(~x); // 0x00/0xFF
            eq_acc[i] = static_cast<unsigned char>(eq_acc[i] & xnor);
            map_nz[i] = static_cast<unsigned char>(map_nz[i] | map_rows[b][i]);
            qry_nz[i] = static_cast<unsigned char>(qry_nz[i] | qry_rows[b][i]);
        }
    }

    for (size_t i = 0; i < DIM; ++i) {
        exp_match_final[i] = static_cast<unsigned char>(eq_acc[i] & (map_nz[i] & qry_nz[i]));
    }
}

// ------------------------------ CIM per-group compute (no debug capture) ------------------------------
static void cim_match_group_presence(
    CimModule& cim,
    int g // group index
){
    const uint8_t  bytemask = 0xFF;
    const uint32_t bankmask = 0xFFFFFFFFu;
    const uint64_t colmask  = 0xFFFFFFFFFFFFFFFFull;

    // Reset scratch rows
    uint8_t zeros[DIM], ones[DIM];
    memset(zeros, static_cast<unsigned char>(0x00), DIM);
    memset(ones,  static_cast<unsigned char>(0xFF), DIM);
    cim.copy_to_cim(SCR_EQACC_ROW,  ones,  DIM); // EQ_ACC = all 1s
    cim.copy_to_cim(SCR_MAPNZ_ROW,  zeros, DIM); // MAP_NZ = 0
    cim.copy_to_cim(SCR_QRYNZ_ROW,  zeros, DIM); // QRY_NZ = 0

    for (int b = 0; b < PLANES; ++b) {
        uint8_t map_row_b = static_cast<uint8_t>(row_plane_of_group(g, b));
        uint8_t qry_row_b = static_cast<uint8_t>(QRY_BASE_ROW + b);

        // XOR → SCR_XOR_ROW
        cim.XOR({map_row_b, qry_row_b}, bytemask, bankmask, colmask, SCR_XOR_ROW);
        cim.COPY(SCR_XOR_ROW, (0x100u | SCR_XOR_ROW)); // materialize

        // XNOR → SCR_XNOR_ROW
        cim.NOT_COND(SCR_XNOR_ROW, SCR_XOR_ROW, true, false, bytemask, bankmask, colmask);

        // EQ_ACC = EQ_ACC & XNOR
        cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_XNOR_ROW)},
                bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(SCR_EQACC_ROW, (0x100u | SCR_MATCH_ROW));

        // MAP_NZ |= MAP_plane
        cim.OR({static_cast<uint8_t>(SCR_MAPNZ_ROW), map_row_b},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(SCR_MAPNZ_ROW, (0x100u | SCR_MATCH_ROW));

        // QRY_NZ |= QRY_plane
        cim.OR({static_cast<uint8_t>(SCR_QRYNZ_ROW), qry_row_b},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(SCR_QRYNZ_ROW, (0x100u | SCR_MATCH_ROW));
    }

    // BOTH_NZ = MAP_NZ & QRY_NZ
    cim.AND({static_cast<uint8_t>(SCR_MAPNZ_ROW), static_cast<uint8_t>(SCR_QRYNZ_ROW)},
            bytemask, bankmask, colmask, SCR_BOTHNZ_ROW);
    cim.COPY(SCR_BOTHNZ_ROW, (0x100u | SCR_BOTHNZ_ROW));

    // MATCH = EQ_ACC & BOTH_NZ
    cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_BOTHNZ_ROW)},
            bytemask, bankmask, colmask, SCR_MATCH_ROW);
    cim.COPY(SCR_MATCH_ROW, (0x100u | SCR_MATCH_ROW));
}

// ------------------------------ Types ------------------------------
struct MapScore {
    uint64_t map_id;
    uint64_t query_id;
    int      group_idx;
    int      seg_idx;      // 0..7
    size_t   bits_equal;   // 0..512
    size_t   dims_equal;   // 0..64
};

struct Mismatch {
    int      group_idx;
    int      seg_idx;
    uint64_t map_id;
    size_t   hw_bits, hw_dims;
    size_t   exp_bits, exp_dims;
};

// ------------------------------ Main ------------------------------
static void gen_descriptor_for_map(int m, uint8_t out_desc[DESC_BYTES]) {
    // Deterministic generator (simple and reproducible)
    for (int i = 0; i < DESC_BYTES; ++i) {
        out_desc[i] = static_cast<unsigned char>((i * 3 + 7 + m * 5) & 0xFF);
    }
}

int main() {
    printf("Array: %dx%d (rows x cols), DESC_BYTES=%d, PLANES=%d\n",
           ROWS, DIM, DESC_BYTES, PLANES);
    printf("Groups=%d, Maps/group=%d, Total maps=%d\n",
           GROUPS, MAPS_PER_GROUP, MAPS_TOTAL);

    CimModule cim;

    // Prepare all maps (IDs + descriptors)
    vector<uint64_t> all_ids(MAPS_TOTAL);
    vector< array<uint8_t, DESC_BYTES> > all_desc(MAPS_TOTAL);

    for (int m = 0; m < MAPS_TOTAL; ++m) {
        all_ids[m] = 0xABCDEF0000000000ull ^ static_cast<uint64_t>(m * 0x9E3779B97F4A7C15ull);
        gen_descriptor_for_map(m, all_desc[m].data());
    }

    // Clear the whole array once (safety)
    uint8_t zeros[DIM]; memset(zeros, static_cast<unsigned char>(0x00), DIM);
    for (int r = 0; r < ROWS; ++r) cim.copy_to_cim(r, zeros, DIM);

    // Load all groups to CIM (ID row + 8 plane rows)
    uint8_t id_row_buf[DIM];
    uint8_t map_rows_buf[PLANES][DIM];

    for (int g = 0; g < GROUPS; ++g) {
        // Collect this group's 8 descriptors
        uint8_t map8[MAPS_PER_GROUP][DESC_BYTES];
        uint64_t ids8[MAPS_PER_GROUP];
        for (int s = 0; s < MAPS_PER_GROUP; ++s) {
            int m = g * MAPS_PER_GROUP + s;
            ids8[s] = all_ids[m];
            memcpy(map8[s], all_desc[m].data(), DESC_BYTES);
        }

        // ID row
        pack_id_row_8x64(ids8, id_row_buf);
        cim.copy_to_cim(row_id_of_group(g), id_row_buf, DIM);

        // 8 plane rows
        build_map_rows(map_rows_buf, map8);
        for (int b = 0; b < PLANES; ++b) {
            cim.copy_to_cim(row_plane_of_group(g, b), (void*)map_rows_buf[b], DIM);
        }
    }

    // Build NUM_QUERIES queries (deterministic tweaks)
    int q_indices[NUM_QUERIES] = {37, 5, 80, 123, 150, 0, 199, 88};

    // Prepare log.txt
    ofstream log("log.txt", ios::out | ios::trunc);
    if (!log) {
        fprintf(stderr, "Failed to open log.txt for writing.\n");
        return 1;
    }

    for (int qi = 0; qi < NUM_QUERIES; ++qi) {
        const int q_src_idx = q_indices[qi];
        uint64_t query_id = 0x1234567800000000ull | static_cast<uint64_t>(q_src_idx);

        uint8_t  query[DESC_BYTES];
        memcpy(query, all_desc[q_src_idx].data(), DESC_BYTES);

        // Deterministic differences based on qi
        query[0] = static_cast<unsigned char>(query[0] ^ (qi * 0x11));
        for (int i = 10; i <= 15; ++i) {
            query[i] = static_cast<unsigned char>(query[i] ^ (0x0F ^ (qi & 0x3)));
        }

        // Build query rows (for CPU expected and as source for CIM write)
        uint8_t qry_rows[PLANES][DIM]; memset(qry_rows, 0, sizeof(qry_rows));
        build_query_rows(qry_rows, query);

        // Write query planes to rows 233..240
        for (int b = 0; b < PLANES; ++b) {
            cim.copy_to_cim(QRY_BASE_ROW + b, (void*)qry_rows[b], DIM);
        }

        // Sweep all groups; compute HW score, compute CPU expected, then compare
        vector<Mismatch> mismatches;
        mismatches.reserve(16);

        for (int g = 0; g < GROUPS; ++g) {
            // CPU expected: rebuild this group's 8 plane rows from descriptors
            uint8_t map8[MAPS_PER_GROUP][DESC_BYTES];
            for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                int m = g * MAPS_PER_GROUP + s;
                memcpy(map8[s], all_desc[m].data(), DESC_BYTES);
            }
            uint8_t map_rows_for_cpu[PLANES][DIM];
            build_map_rows(map_rows_for_cpu, map8);

            uint8_t exp_match_row[DIM]; memset(exp_match_row, 0, sizeof(exp_match_row));
            cpu_expected_match_row(map_rows_for_cpu, qry_rows, exp_match_row);

            // CIM: compute + read back MATCH row and ID row
            cim_match_group_presence(cim, g);

            uint8_t match_row[DIM]; memset(match_row, 0, sizeof(match_row));
            uint8_t id_row[DIM];    memset(id_row,    0, sizeof(id_row));
            cim.copy_to_cpu(match_row, SCR_MATCH_ROW, DIM);
            cim.copy_to_cpu(id_row, row_id_of_group(g), DIM);

            // Compare per 64-byte segment (8 segments)
            for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                const uint8_t* hw_seg  = &match_row[s * DESC_BYTES];
                const uint8_t* exp_seg = &exp_match_row[s * DESC_BYTES];

                size_t hw_bits  = popcount_bytes(hw_seg,  DESC_BYTES);
                size_t exp_bits = popcount_bytes(exp_seg, DESC_BYTES);

                if (hw_bits != exp_bits) {
                    Mismatch mm{};
                    mm.group_idx = g;
                    mm.seg_idx   = s;
                    mm.map_id    = unpack_id_from_row_seg(id_row, s); // should equal all_ids[g*8+s]
                    mm.hw_bits   = hw_bits;
                    mm.exp_bits  = exp_bits;
                    mm.hw_dims   = hw_bits / 8;
                    mm.exp_dims  = exp_bits / 8;
                    if (mismatches.size() < 50) mismatches.push_back(mm); // at most 50 entries
                }
            }
        }

        // Write comparison result for this query to log.txt
        if (mismatches.empty()) {
            log << "[Query " << qi << "] ID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)query_id << dec
                << " : OK (all 200 maps match expected scores)\n";
        } else {
            log << "[Query " << qi << "] ID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)query_id << dec
                << " : MISMATCH count=" << (int)mismatches.size()
                << " (showing up to 50)\n";
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
    }

    log.flush();
    log.close();

    printf("All %d queries processed. Summary written to log.txt\n", NUM_QUERIES);
    return 0;
}