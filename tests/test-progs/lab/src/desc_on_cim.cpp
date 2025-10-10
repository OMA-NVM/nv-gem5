// desc_multiq_64banks_fullfill.cpp
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
#define ROWS        256
#define DIM         (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t)) // expect 64
#define DESC_BYTES  64
#define PLANES      8

#define MAP_ROWS_RESERVED   225
#define GROUP_ROWS          9
#define GROUPS              (MAP_ROWS_RESERVED / GROUP_ROWS) // 25
#define MAPS_PER_GROUP      8
#define MAPS_PER_BANK       (GROUPS * MAPS_PER_GROUP)        // 200

#define QRY_BASE_ROW        233
#define SCR_XOR_ROW         241
#define SCR_XNOR_ROW        242
#define SCR_EQACC_ROW       243
#define SCR_MAPNZ_ROW       244
#define SCR_QRYNZ_ROW       245
#define SCR_BOTHNZ_ROW      246
#define SCR_MATCH_ROW       247

// Number of queries to test (can be adjusted)
#define NUM_QUERIES         8

#define PLANE_BITS          64
#define PLANE_BYTES         (PLANE_BITS / 8)  // 8

// 64 banks: indices 0..63
static constexpr int      NUM_BANK_BITS = 6;   // 2^6 = 64 banks
static constexpr uint64_t MASK_BANK_ALL = ~0ull;     // all 64 bits set
static constexpr uint64_t COLMASK_ALL   = 0xFFull;   // 8 sub-columns (8B each)

static_assert(ROWS >= 248, "Need at least rows up to 247 for query/scratch.");
static_assert(DIM  == 64,  "Expect 64 bytes per row (num_column_bits = 6).");
static_assert(MAPS_PER_GROUP * PLANE_BYTES == DIM,
              "8 maps x 8B per plane must fill one row (64B).");
static_assert(MAP_ROWS_RESERVED % GROUP_ROWS == 0, "MAP rows must be multiple of 9.");
static_assert(NUM_BANK_BITS == 6, "This test assumes 64 banks.");

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

static void to_bitplanes64(const uint8_t desc[DESC_BYTES], uint8_t planes[PLANES][PLANE_BYTES]) {
    memset(planes, 0, PLANES * PLANE_BYTES);
    for (int d = 0; d < 64; ++d) {
        int byte_idx = d >> 3;
        int bit_pos  = d & 7;
        uint8_t mask = static_cast<uint8_t>(1u << bit_pos);
        uint8_t v    = desc[d];
        for (int b = 0; b < PLANES; ++b) {
            if ((v >> b) & 1u) planes[b][byte_idx] |= mask;
        }
    }
}

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

static void build_query_rows(uint8_t rows_out[PLANES][DIM], const uint8_t query[DESC_BYTES]) {
    uint8_t bp[PLANES][PLANE_BYTES];
    to_bitplanes64(query, bp);
    for (int b = 0; b < PLANES; ++b) {
        for (int k = 0; k < MAPS_PER_GROUP; ++k) {
            memcpy(&rows_out[b][k * PLANE_BYTES], bp[b], PLANE_BYTES);
        }
    }
}

static inline int row_id_of_group(int g)           { return 9*g + 0; }
static inline int row_plane_of_group(int g, int b) { return 9*g + 1 + b; }

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

// ------------------------------ CPU expected (optional) ------------------------------
// This test ultimately validates using ID pairs, so CPU expected is optional.
// Keep it for debugging if needed.
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

// ------------------------------ CIM per-group compute (banked, arbitrary bankmask) ------------------------------
static void cim_match_group_presence_banked(
    CimModule& cim, int g, uint64_t bankmask
){
    const uint8_t  bytemask = 0xFF;
    const uint64_t colmask  = COLMASK_ALL;

    // Clear scratch rows on all participating banks
    uint8_t zeros[DIM], ones[DIM];
    memset(zeros, 0x00, DIM);
    memset(ones,  0xFF, DIM);
    for (uint8_t bk = 0; bk < (1u << NUM_BANK_BITS); ++bk) {
        if (bankmask & (1ull << bk)) {
            cim.copy_to_cim(bk, SCR_EQACC_ROW,  ones,  NUM_BANK_BITS, DIM);
            cim.copy_to_cim(bk, SCR_MAPNZ_ROW,  zeros, NUM_BANK_BITS, DIM);
            cim.copy_to_cim(bk, SCR_QRYNZ_ROW,  zeros, NUM_BANK_BITS, DIM);
        }
    }

    for (int b = 0; b < PLANES; ++b) {
        uint8_t map_row_b = static_cast<uint8_t>(row_plane_of_group(g, b));
        uint8_t qry_row_b = static_cast<uint8_t>(QRY_BASE_ROW + b);

        cim.XOR({map_row_b, qry_row_b}, bytemask, bankmask, colmask, SCR_XOR_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_XOR_ROW),
                 static_cast<uint16_t>(0x100u | SCR_XOR_ROW),
                 0, bytemask, bankmask, colmask);

        cim.NOT_COND(static_cast<uint16_t>(SCR_XNOR_ROW),
                     static_cast<uint16_t>(SCR_XOR_ROW),
                     true, false, bytemask, bankmask, colmask);

        cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_XNOR_ROW)},
                bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_EQACC_ROW),
                 static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
                 0, bytemask, bankmask, colmask);

        cim.OR({static_cast<uint8_t>(SCR_MAPNZ_ROW), map_row_b},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_MAPNZ_ROW),
                 static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
                 0, bytemask, bankmask, colmask);

        cim.OR({static_cast<uint8_t>(SCR_QRYNZ_ROW), qry_row_b},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(static_cast<uint16_t>(SCR_QRYNZ_ROW),
                 static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
                 0, bytemask, bankmask, colmask);
    }

    cim.AND({static_cast<uint8_t>(SCR_MAPNZ_ROW), static_cast<uint8_t>(SCR_QRYNZ_ROW)},
            bytemask, bankmask, colmask, SCR_BOTHNZ_ROW);
    cim.COPY(static_cast<uint16_t>(SCR_BOTHNZ_ROW),
             static_cast<uint16_t>(0x100u | SCR_BOTHNZ_ROW),
             0, bytemask, bankmask, colmask);

    cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_BOTHNZ_ROW)},
            bytemask, bankmask, colmask, SCR_MATCH_ROW);
    cim.COPY(static_cast<uint16_t>(SCR_MATCH_ROW),
             static_cast<uint16_t>(0x100u | SCR_MATCH_ROW),
             0, bytemask, bankmask, colmask);
}

// ------------------------------ Types ------------------------------
struct BestHit {
    int      bank;         // 0..63
    int      group_idx;    // 0..24
    int      seg_idx;      // 0..7
    uint64_t map_id;
    size_t   bits;         // popcount across the 8 bytes of the segment (0..64)
};

struct QuerySpec {
    int      bank_src;     // source bank for the query
    int      m_idx;        // 0..199
    uint64_t query_id;     // only for labeling
};

// ------------------------------ Generators ------------------------------
// Make per-bank IDs unique and visually identifiable across banks.
static inline uint64_t make_map_id(int bank, int m) {
    // Put bank tag in the high bits: [63:48] = 0xABCD ^ bank
    uint64_t bank_tag = (0xABCDull ^ static_cast<uint64_t>(bank)) << 48;
    uint64_t base     = 0xEF0000000000ull; // fixed base
    uint64_t hashm    = static_cast<uint64_t>(m) * 0x9E3779B97F4A7C15ull;
    return bank_tag | base ^ hashm;
}

// Make descriptors differ across banks (to avoid accidental cross-bank collisions).
static void gen_descriptor_for_map(int bank, int m, uint8_t out_desc[DESC_BYTES]) {
    uint8_t salt_b = static_cast<uint8_t>(0x11 + (bank * 3)); // bank-dependent
    uint8_t salt_m = static_cast<uint8_t>((m * 5) & 0xFF);    // per-map within a bank
    for (int i = 0; i < DESC_BYTES; ++i) {
        // Simple deterministic function
        out_desc[i] = static_cast<unsigned char>((i * 3 + 7 + salt_b) ^ (salt_m + i));
    }
}

// ------------------------------ Main ------------------------------
int main() {
    printf("Array: %dx%d (rows x cols), ROW_BYTES=%d, PLANES=%d, banks=%d (fill ALL banks)\n",
           ROWS, DIM, (int)DIM, PLANES, 1 << NUM_BANK_BITS);
    printf("Groups=%d, Maps/group=%d, Maps/bank=%d, Total maps=%d\n",
           GROUPS, MAPS_PER_GROUP, MAPS_PER_BANK, MAPS_PER_BANK * (1<<NUM_BANK_BITS));

    CimModule cim;

    // Prepare data for all 64 banks
    vector< vector<uint64_t> > all_ids(64);
    vector< vector< array<uint8_t, DESC_BYTES> > > all_desc(64);
    for (int bank = 0; bank < 64; ++bank) {
        all_ids[bank].resize(MAPS_PER_BANK);
        all_desc[bank].resize(MAPS_PER_BANK);
        for (int m = 0; m < MAPS_PER_BANK; ++m) {
            all_ids[bank][m] = make_map_id(bank, m);
            gen_descriptor_for_map(bank, m, all_desc[bank][m].data());
        }
    }

    // Zero all rows on all banks (0..63)
    uint8_t zeros[DIM]; memset(zeros, 0x00, DIM);
    for (int bank = 0; bank < 64; ++bank) {
        for (int r = 0; r < ROWS; ++r)
            cim.copy_to_cim(static_cast<uint8_t>(bank), r, zeros, NUM_BANK_BITS, DIM);
    }

    // Load all 25 groups for every bank (per group: 1 ID row + 8 plane rows)
    uint8_t id_row_buf[DIM];
    uint8_t map_rows_buf[PLANES][DIM];

    for (int bank = 0; bank < 64; ++bank) {
        for (int g = 0; g < GROUPS; ++g) {
            uint8_t  map8[MAPS_PER_GROUP][DESC_BYTES];
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

    // Prepare 8 queries (source banks are spread out)
    const QuerySpec queries[NUM_QUERIES] = {
        {  0,  37, 0x5100000000000025ull }, // arbitrary query_id for labeling
        {  5,   5, 0x5100000000000005ull },
        { 10,  80, 0x5100000000000050ull },
        { 20, 123, 0x510000000000007bull },
        { 40, 150, 0x5100000000000096ull },
        { 50,   0, 0x5100000000000000ull },
        { 63, 199, 0x51000000000000c7ull },
        { 22,  88, 0x5100000000000058ull }
    };

    ofstream log("log.txt", ios::out | ios::trunc);
    if (!log) { fprintf(stderr, "Failed to open log.txt\n"); return 1; }

    const uint8_t  bytemask = 0xFF;
    const uint64_t colmask  = COLMASK_ALL;

    // --- Process queries one by one ---
    for (int qi = 0; qi < NUM_QUERIES; ++qi) {
        const int q_bank = queries[qi].bank_src;
        const int q_m    = queries[qi].m_idx;
        const uint64_t query_id = queries[qi].query_id;

        // Take the exact descriptor of (q_bank, q_m) as the query (no tweak)
        uint8_t query[DESC_BYTES];
        memcpy(query, all_desc[q_bank][q_m].data(), DESC_BYTES);

        // Build the 8 query planes and broadcast to all banks at QRY_BASE_ROW..+7
        uint8_t qry_rows[PLANES][DIM]; memset(qry_rows, 0, sizeof(qry_rows));
        build_query_rows(qry_rows, query);
        for (int bank = 0; bank < 64; ++bank) {
            for (int b = 0; b < PLANES; ++b) {
                cim.copy_to_cim(static_cast<uint8_t>(bank),
                                QRY_BASE_ROW + b, (void*)qry_rows[b], NUM_BANK_BITS, DIM);
            }
        }

        // Find top-1 match across all banks × all groups × 8 segments per group
        BestHit best{ -1, -1, -1, 0ull, 0 };

        for (int g = 0; g < GROUPS; ++g) {
            // Execute CIM on all banks together (bankmask = all ones)
            cim_match_group_presence_banked(cim, g, MASK_BANK_ALL);

            // Read MATCH and ID rows from each bank for this group; update top-1
            for (int bank = 0; bank < 64; ++bank) {
                uint8_t match_row[DIM], id_row[DIM];
                memset(match_row, 0, DIM); memset(id_row, 0, DIM);

                cim.copy_to_cpu(match_row, static_cast<uint8_t>(bank),
                                SCR_MATCH_ROW, NUM_BANK_BITS, DIM);
                cim.copy_to_cpu(id_row,    static_cast<uint8_t>(bank),
                                row_id_of_group(g), NUM_BANK_BITS, DIM);

                for (int s = 0; s < MAPS_PER_GROUP; ++s) {
                    // Score = popcount(match) over the 8 bytes of this segment
                    const uint8_t* seg = &match_row[s * PLANE_BYTES];
                    size_t bits = popcount_bytes(seg, PLANE_BYTES);
                    if (bits > best.bits) {
                        best.bits      = bits;
                        best.bank      = bank;
                        best.group_idx = g;
                        best.seg_idx   = s;
                        best.map_id    = unpack_id_from_row_seg(id_row, s);
                    }
                }
            }
        }

        // Expected top-1 is exactly the (q_bank, q_m) descriptor
        const uint64_t exp_map_id = all_ids[q_bank][q_m];

        // Dump result (validate by ID pair)
        if (best.map_id == exp_map_id) {
            log << "[Query " << qi << "] src(B" << setw(2) << setfill('0') << q_bank
                << ", m=" << setw(3) << setfill('0') << q_m << ") "
                << "QID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)query_id << dec
                << " : OK  best={B" << setw(2) << setfill('0') << best.bank
                << ", g=" << setw(2) << setfill('0') << best.group_idx
                << ", seg=" << best.seg_idx << "} "
                << "MapID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)best.map_id << dec
                << "  score(bits)=" << best.bits << "\n";
        } else {
            log << "[Query " << qi << "] src(B" << setw(2) << setfill('0') << q_bank
                << ", m=" << setw(3) << setfill('0') << q_m << ") "
                << "QID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)query_id << dec
                << " : MISMATCH  expected MapID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)exp_map_id << dec
                << "  but best={B" << setw(2) << setfill('0') << best.bank
                << ", g=" << setw(2) << setfill('0') << best.group_idx
                << ", seg=" << best.seg_idx << "} "
                << "MapID=0x" << hex << setw(16) << setfill('0')
                << (unsigned long long)best.map_id << dec
                << "  score(bits)=" << best.bits << "\n";
        }
    }

    log.flush();
    log.close();
    printf("All %d queries processed on ALL banks. Summary written to log.txt\n", NUM_QUERIES);
    return 0;
}