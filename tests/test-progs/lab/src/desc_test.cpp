#include "cim_api.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
#include <array>
#include <iomanip>

using namespace std;

// ------------------------------ Config ------------------------------
// 256 rows × 512 cols（每列 512B）
#define ROWS        256
#define DIM         (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t)) // expected 512
#define DESC_BYTES  64
#define PLANES      8

// Map區：225 rows = 25 groups * 9 rows/group = 200 maps（每組 8 個 map）
#define MAP_ROWS_RESERVED   225
#define GROUP_ROWS          9
#define GROUPS              (MAP_ROWS_RESERVED / GROUP_ROWS) // 25
#define MAPS_PER_GROUP      8
#define MAPS_TOTAL          (GROUPS * MAPS_PER_GROUP)        // 200

// Query planes + scratch（固定放高位列，皆 < 256，符合 uint8_t row 限制）
// rows 233..240 放 query 的 8 個 bit-planes（每 plane 一列，橫向複製 8 份 64B）
// rows 241..247 當 scratch
#define QRY_BASE_ROW        233       // rows 233..240
#define SCR_XOR_ROW         241
#define SCR_XNOR_ROW        242
#define SCR_EQACC_ROW       243
#define SCR_MAPNZ_ROW       244
#define SCR_QRYNZ_ROW       245
#define SCR_BOTHNZ_ROW      246
#define SCR_MATCH_ROW       247

static_assert(ROWS >= 248, "Need at least rows up to 247 for query/scratch.");
static_assert(DIM  == 512, "This test expects 512 columns per row.");
static_assert(DESC_BYTES * 8 == DIM, "8 descriptors of 64B must fill one row.");
static_assert(MAP_ROWS_RESERVED % GROUP_ROWS == 0, "MAP rows must be multiple of 9.");

// ------------------------------ Logger API (from debug_log.cpp) ------------------------------
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
);

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

// Convert a byte coded as 0x00/0xFF to bit 0/1 (tolerate other values).
static inline int byte_to_bit(uint8_t v) {
    if (v == static_cast<unsigned char>(0x00)) return 0;
    if (v == static_cast<unsigned char>(0xFF)) return 1;
    return (v ? 1 : 0);
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
static inline int row_id_of_group(int g)               { return 9*g + 0; }
static inline int row_plane_of_group(int g, int b)     { return 9*g + 1 + b; }

// Pack 8×64-bit IDs into one 512-column row（每 ID 64 欄，LSB→MSB）
static void pack_id_row_8x64(const uint64_t ids[MAPS_PER_GROUP], uint8_t out_row[DIM]) {
    memset(out_row, 0, DIM);
    for (int s = 0; s < MAPS_PER_GROUP; ++s) {
        for (int i = 0; i < 64; ++i) {
            int bit = ( (ids[s] >> i) & 1ull ) ? 1 : 0; // LSB first
            out_row[s * 64 + i] = static_cast<unsigned char>(bit ? 0xFF : 0x00);
        }
    }
}

// Unpack one 64-bit ID from a 512-column ID row segment（seg 0..7）
static uint64_t unpack_id_from_row_seg(const uint8_t id_row[DIM], int seg /*0..7*/) {
    uint64_t id = 0;
    const uint8_t* p = &id_row[seg * 64];
    for (int i = 0; i < 64; ++i) {
        if (byte_to_bit(p[i])) id |= (1ull << i); // LSB first
    }
    return id;
}

// ------------------------------ CIM per-group compute (with optional debug capture) ------------------------------
static void cim_match_group_presence_debug(
    CimModule& cim,
    int g,                                      // group index
    // optional per-plane actual snapshots (capture if not-null)
    uint8_t (*act_xor)[DIM],
    uint8_t (*act_xnor)[DIM],
    uint8_t (*act_eqacc_after)[DIM],
    uint8_t (*act_mapnz_after)[DIM],
    uint8_t (*act_qrynz_after)[DIM],
    uint8_t act_bothnz_final[DIM],
    uint8_t act_match_final[DIM]
){
    const uint8_t  bytemask = 0xFF;
    const uint32_t bankmask = 0xFFFFFFFFu;
    const uint64_t colmask  = 0xFFFFFFFFFFFFFFFFull;

    // Reset scratch for this group
    uint8_t zeros[DIM], ones[DIM];
    memset(zeros, static_cast<unsigned char>(0x00), DIM);
    memset(ones,  static_cast<unsigned char>(0xFF), DIM);
    cim.copy_to_cim(SCR_EQACC_ROW,  ones,  DIM); // EQ_ACC = all 1s
    cim.copy_to_cim(SCR_MAPNZ_ROW,  zeros, DIM); // MAP_NZ = 0
    cim.copy_to_cim(SCR_QRYNZ_ROW,  zeros, DIM); // QRY_NZ = 0

    for (int b = 0; b < PLANES; ++b) {
        int map_row_b = row_plane_of_group(g, b);
        int qry_row_b = QRY_BASE_ROW + b;

        // XOR → SCR_XOR_ROW
        cim.XOR({static_cast<uint8_t>(map_row_b), static_cast<uint8_t>(qry_row_b)},
                bytemask, bankmask, colmask, SCR_XOR_ROW);
        cim.COPY(SCR_XOR_ROW, (0x100u | SCR_XOR_ROW));
        if (act_xor) cim.copy_to_cpu(act_xor[b], SCR_XOR_ROW, DIM);

        // XNOR → SCR_XNOR_ROW (NOT of XOR)
        cim.NOT_COND(SCR_XNOR_ROW, SCR_XOR_ROW, true, false, bytemask, bankmask, colmask);
        if (act_xnor) cim.copy_to_cpu(act_xnor[b], SCR_XNOR_ROW, DIM);

        // EQ_ACC = EQ_ACC & XNOR
        cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_XNOR_ROW)},
                bytemask, bankmask, colmask, SCR_MATCH_ROW); // temp
        cim.COPY(SCR_EQACC_ROW, (0x100u | SCR_MATCH_ROW));
        if (act_eqacc_after) cim.copy_to_cpu(act_eqacc_after[b], SCR_EQACC_ROW, DIM);

        // MAP_NZ |= MAP_plane
        cim.OR({static_cast<uint8_t>(SCR_MAPNZ_ROW), static_cast<uint8_t>(map_row_b)},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(SCR_MAPNZ_ROW, (0x100u | SCR_MATCH_ROW));
        if (act_mapnz_after) cim.copy_to_cpu(act_mapnz_after[b], SCR_MAPNZ_ROW, DIM);

        // QRY_NZ |= QRY_plane
        cim.OR({static_cast<uint8_t>(SCR_QRYNZ_ROW), static_cast<uint8_t>(qry_row_b)},
               bytemask, bankmask, colmask, SCR_MATCH_ROW);
        cim.COPY(SCR_QRYNZ_ROW, (0x100u | SCR_MATCH_ROW));
        if (act_qrynz_after) cim.copy_to_cpu(act_qrynz_after[b], SCR_QRYNZ_ROW, DIM);
    }

    // BOTH_NZ = MAP_NZ & QRY_NZ
    cim.AND({static_cast<uint8_t>(SCR_MAPNZ_ROW), static_cast<uint8_t>(SCR_QRYNZ_ROW)},
            bytemask, bankmask, colmask, SCR_BOTHNZ_ROW);
    cim.COPY(SCR_BOTHNZ_ROW, (0x100u | SCR_BOTHNZ_ROW));
    if (act_bothnz_final) cim.copy_to_cpu(act_bothnz_final, SCR_BOTHNZ_ROW, DIM);

    // MATCH = EQ_ACC & BOTH_NZ
    cim.AND({static_cast<uint8_t>(SCR_EQACC_ROW), static_cast<uint8_t>(SCR_BOTHNZ_ROW)},
            bytemask, bankmask, colmask, SCR_MATCH_ROW);
    cim.COPY(SCR_MATCH_ROW, (0x100u | SCR_MATCH_ROW));
    if (act_match_final) cim.copy_to_cpu(act_match_final, SCR_MATCH_ROW, DIM);
}

// ------------------------------ Main ------------------------------
struct MapScore {
    uint64_t map_id;
    uint64_t query_id;
    int      group_idx;
    int      seg_idx;
    size_t   bits_equal;
    size_t   dims_equal;
};

static void gen_descriptor_for_map(int m, uint8_t out_desc[DESC_BYTES]) {
    // 決定論產生器（簡單、可重現）
    // base = (i*3 + 7 + m*5) & 0xFF
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

    // --- Prepare all maps (IDs + descriptors) ---
    vector<uint64_t> all_ids(MAPS_TOTAL);
    vector< array<uint8_t, DESC_BYTES> > all_desc(MAPS_TOTAL);

    for (int m = 0; m < MAPS_TOTAL; ++m) {
        all_ids[m] = 0xABCDEF0000000000ull ^ static_cast<uint64_t>(m * 0x9E3779B97F4A7C15ull);
        gen_descriptor_for_map(m, all_desc[m].data());
    }

    // Choose a query from one of the maps, and tweak a bit
    const int q_src_idx = 37; // 0..199
    uint64_t query_id = 0x1234567800000000ull | static_cast<uint64_t>(q_src_idx);
    uint8_t  query[DESC_BYTES];
    memcpy(query, all_desc[q_src_idx].data(), DESC_BYTES);
    query[0] = static_cast<unsigned char>(0x00);
    for (int i = 10; i <= 15; ++i) query[i] = static_cast<unsigned char>(query[i] ^ 0x0F);

    // Build query planes and load to fixed rows
    uint8_t qry_rows[PLANES][DIM]; memset(qry_rows, 0, sizeof(qry_rows));
    build_query_rows(qry_rows, query);

    uint8_t zeros[DIM]; memset(zeros, static_cast<unsigned char>(0x00), DIM);
    for (int r = 0; r < ROWS; ++r) cim.copy_to_cim(r, zeros, DIM);
    for (int b = 0; b < PLANES; ++b) {
        cim.copy_to_cim(QRY_BASE_ROW + b, (void*)qry_rows[b], DIM);
    }

    // --- Debug captures for group 0 ---
    uint8_t dbg_map8[MAPS_PER_GROUP][DESC_BYTES];
    uint8_t map_rows_g0[PLANES][DIM]; memset(map_rows_g0, 0, sizeof(map_rows_g0));
    uint8_t act_rows_map_g0[PLANES][DIM]; memset(act_rows_map_g0, 0, sizeof(act_rows_map_g0));
    uint8_t act_rows_qry_g[PLANES][DIM];  memset(act_rows_qry_g,  0, sizeof(act_rows_qry_g));
    uint8_t act_xor[PLANES][DIM], act_xnor[PLANES][DIM], act_eqacc_after[PLANES][DIM],
            act_mapnz_after[PLANES][DIM], act_qrynz_after[PLANES][DIM],
            act_bothnz_final[DIM], act_match_final[DIM];
    memset(act_xor, 0, sizeof(act_xor));
    memset(act_xnor, 0, sizeof(act_xnor));
    memset(act_eqacc_after, 0, sizeof(act_eqacc_after));
    memset(act_mapnz_after, 0, sizeof(act_mapnz_after));
    memset(act_qrynz_after, 0, sizeof(act_qrynz_after));
    memset(act_bothnz_final, 0, sizeof(act_bothnz_final));
    memset(act_match_final, 0, sizeof(act_match_final));

    // --- Load all groups to CIM (ID row + 8 plane rows) ---
    uint8_t id_row_buf[DIM];
    uint8_t map_rows_buf[PLANES][DIM];

    for (int g = 0; g < GROUPS; ++g) {
        // collect 8 descriptors of this group
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

        // Snapshot for group 0
        if (g == 0) {
            memcpy(dbg_map8, map8, sizeof(dbg_map8));
            memcpy(map_rows_g0, map_rows_buf, sizeof(map_rows_g0));
            for (int b = 0; b < PLANES; ++b) {
                cim.copy_to_cpu(act_rows_map_g0[b], row_plane_of_group(0, b), DIM);
                cim.copy_to_cpu(act_rows_qry_g[b],  QRY_BASE_ROW + b, DIM);
            }
        }
    }

    // --- Run compute group by group; collect scores ---
    vector<MapScore> scores;
    scores.reserve(MAPS_TOTAL);

    for (int g = 0; g < GROUPS; ++g) {
        if (g == 0) {
            cim_match_group_presence_debug(cim, g,
                                           act_xor, act_xnor, act_eqacc_after,
                                           act_mapnz_after, act_qrynz_after,
                                           act_bothnz_final, act_match_final);
        } else {
            cim_match_group_presence_debug(cim, g,
                                           nullptr, nullptr, nullptr,
                                           nullptr, nullptr,
                                           nullptr, nullptr);
        }

        // Read MATCH row and the group's ID row; compute 8 segment scores
        uint8_t match_row[DIM]; memset(match_row, 0, sizeof(match_row));
        uint8_t id_row[DIM];    memset(id_row,    0, sizeof(id_row));
        cim.copy_to_cpu(match_row, SCR_MATCH_ROW, DIM);
        cim.copy_to_cpu(id_row, row_id_of_group(g), DIM);

        for (int s = 0; s < MAPS_PER_GROUP; ++s) {
            MapScore sc{};
            sc.group_idx   = g;
            sc.seg_idx     = s;
            sc.map_id      = unpack_id_from_row_seg(id_row, s);
            sc.query_id    = query_id;

            const uint8_t* seg = &match_row[s * DESC_BYTES];
            sc.bits_equal  = popcount_bytes(seg, DESC_BYTES);
            sc.dims_equal  = sc.bits_equal / 8;
            scores.push_back(sc);
        }
    }

    // --- Quick console scores (all maps) ---
    printf("QueryID = 0x%016llx\n", (unsigned long long)query_id);
    for (size_t i = 0; i < scores.size(); ++i) {
        const auto& s = scores[i];
        printf("[g%02d s%d] MapID=0x%016llx  Score(dims)=%zu/64  Bits=%zu/512\n",
               s.group_idx, s.seg_idx,
               (unsigned long long)s.map_id,
               s.dims_equal, s.bits_equal);
    }

    // --- Debug log for group 0 ---
    write_debug_log_full(
        dbg_map8, query,
        map_rows_g0, qry_rows,
        act_rows_map_g0, act_rows_qry_g,
        // actual
        act_xor, act_xnor, act_eqacc_after,
        act_mapnz_after, act_qrynz_after,
        act_bothnz_final, act_match_final,
        QRY_BASE_ROW
    );

    printf("Debug log written to log.txt (group0 snapshots + expected compare).\n");
    return 0;
}