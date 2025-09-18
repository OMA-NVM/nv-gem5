#include "cim_api.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace std;

#define DIM (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t))

static inline void print_row_bits(const char* label, const uint8_t* p, size_t n) {
    printf("%s", label);
    for (size_t i = 0; i < n; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            printf("%d", (p[i] >> bit) & 1);
        }
        printf(" ");
    }
    printf("\n");
}

// convert to bit-plane: plane[b][i] = 0x00/0xFF
static void to_bitplanes(const uint8_t desc[DIM], uint8_t plane[8][DIM]) {
    for (int b = 0; b < 8; ++b) {
        for (int i = 0; i < DIM; ++i) {
            plane[b][i] = ((desc[i] >> b) & 1) ? 0xFF : 0x00;
        }
    }
}

static void cim_match(const uint8_t map_bp[8][DIM],
                      const uint8_t qry_bp[8][DIM],
                      uint8_t match_out[DIM]) {
    CimModule cim;

    const uint8_t bytemask = 0xFF;
    const uint32_t bankmask = 0xFFFFFFFFu;
    const uint64_t colmask = 0xFFFFFFFFFFFFFFFFull;

    uint8_t all_ones[DIM];
    for (int i = 0; i < DIM; ++i) all_ones[i] = 0xFF;

    cim.copy_to_cim(4, all_ones, DIM);

    uint8_t all_zero[DIM] = {0};
    cim.copy_to_cim(6, all_zero, DIM);
    cim.copy_to_cim(7, all_zero, DIM);

    for (int b = 0; b < 8; ++b) {
        cim.copy_to_cim(0, (void*)map_bp[b], DIM);
        cim.copy_to_cim(1, (void*)qry_bp[b], DIM);

        cim.XOR({0, 1}, bytemask, bankmask, colmask, 2);
        cim.COPY(2, (0x100u | 2));

        cim.NOT_COND(3, 2, true, false);

        // eq_acc = AND(eq_acc, row3)
        cim.AND({4, 3}, bytemask, bankmask, colmask, 5);
        cim.COPY(4, (0x100u | 5));

        // map_nz = OR(map_nz, row0)
        cim.OR({6, 0}, bytemask, bankmask, colmask, 5);
        cim.COPY(6, (0x100u | 5));

        // qry_nz = OR(qry_nz, row1)
        cim.OR({7, 1}, bytemask, bankmask, colmask, 5);
        cim.COPY(7, (0x100u | 5));
    }

    // both_nonzero = (map_nz OR qry_nz)
    cim.OR({6, 7}, bytemask, bankmask, colmask, 5);
    cim.COPY(8, (0x100u | 5));

    // match = eq_acc AND both_nonzero
    cim.AND({4, 8}, bytemask, bankmask, colmask, 5);
    cim.COPY(4, (0x100u | 5));     

    cim.copy_to_cpu(match_out, 4, DIM);
}

static int handle(const uint8_t desc_map[DIM], const uint8_t desc_query[DIM]) {
    uint8_t map_bp[8][DIM];
    uint8_t qry_bp[8][DIM];
    uint8_t match_mask[DIM];

    to_bitplanes(desc_map, map_bp);
    to_bitplanes(desc_query, qry_bp);

    // print_row_bits("MAP   :", desc_map, DIM);
    for(int b = 0; b < 8; ++b) {
        char buf[32];
        snprintf(buf, sizeof(buf), "MAP B%d:", b);
        // print_row_bits(buf, map_bp[b], DIM);
    }
    // print_row_bits("QUERY :", desc_query, DIM);
    for(int b = 0; b < 8; ++b) {
        char buf[32];
        snprintf(buf, sizeof(buf), "QRY B%d:", b);
        // print_row_bits(buf, qry_bp[b], DIM);
    }

    cim_match(map_bp, qry_bp, match_mask);

    int score = 0;
    for (int i = 0; i < DIM; ++i) {
        if (match_mask[i] == 0xFF) score++;
    }

    print_row_bits("MAP   :", desc_map, DIM);
    print_row_bits("QUERY :", desc_query, DIM);
    print_row_bits("MATCH :", match_mask, DIM);

    return score;
}

int main() {
    printf("DIM: %d\n", DIM);

    uint8_t desc_map[DIM];
    uint8_t desc_query[DIM];

    for (int i = 0; i < DIM; ++i) {
        desc_map[i] = static_cast<uint8_t>((i * 3 + 7) & 0xFF);
    }

    for (int i = 0; i < DIM; ++i) {
        desc_query[i] = desc_map[i];
    }

    // 規定變更：
    // 1) 第一個 byte 設為 0x00
    desc_query[0] = 0x00;

    // 2) 將 [10..15] 這段改成不同數字（用 XOR 讓它確實不同）
    const int mutBeg = 10;
    const int mutEnd = 15;
    for (int i = mutBeg; i <= mutEnd && i < DIM; ++i) {
        desc_query[i] ^= 0x0F;  // 翻轉低 4 bits，確保與 map 不同
    }

    int score = handle(desc_map, desc_query);
    printf("Score = %d (out of %d)\n", score, DIM);
    return 0;
}