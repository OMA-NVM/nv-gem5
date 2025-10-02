#include "cim_api.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace std;

#define ROW_BYTES (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t))
static_assert(ROW_BYTES == 64, "Expect 64 bytes per row when num_column_bits=6.");

static inline void gen_bank0_A(uint8_t* d){ for (int i=0;i<64;++i) d[i]=uint8_t(0x10 + i); }
static inline void gen_bank0_B(uint8_t* d){ for (int i=0;i<64;++i) d[i]=uint8_t(0x90 ^ uint8_t(i*3)); }
static inline void gen_bank1_A(uint8_t* d){ for (int i=0;i<64;++i) d[i]=uint8_t(0xA0 + (i*5)); }
static inline void gen_bank1_B(uint8_t* d){ for (int i=0;i<64;++i) d[i]=uint8_t(0x5F ^ uint8_t(i*7)); }

static void pack_planes64(const uint8_t in[64], uint8_t out_row[64]) {
    uint8_t bp[8][8]; std::memset(bp, 0, sizeof(bp));
    for (int d = 0; d < 64; ++d) {
        int byte_idx = d >> 3;
        int bit_pos  = d & 7;
        uint8_t m    = uint8_t(1u << bit_pos);
        uint8_t v    = in[d];
        for (int b = 0; b < 8; ++b) if ((v >> b) & 1u) bp[b][byte_idx] |= m;
    }
    for (int b = 0; b < 8; ++b) memcpy(out_row + b*8, bp[b], 8);
}

static void raw_xor_then_pack(const uint8_t a[64], const uint8_t b[64], uint8_t out_row[64]) {
    uint8_t x[64];
    for (int i=0;i<64;++i) x[i] = uint8_t(a[i] ^ b[i]);
    pack_planes64(x, out_row);
}

static size_t diff_bytes(const uint8_t* a, const uint8_t* b, size_t n, size_t* first_idx) {
    size_t d=0, idx=(size_t)-1;
    for (size_t i=0;i<n;++i){ if(a[i]!=b[i]){ if(idx==(size_t)-1) idx=i; ++d; } }
    if (first_idx) *first_idx = (idx==(size_t)-1?0:idx);
    return d;
}

static void hexdump16(const char* tag, const uint8_t* p){
    printf("%s:", tag);
    for (int i=0;i<16;++i) printf(" %02X", p[i]);
    printf(" ...\n");
}

int main(){
    const int      num_bank_bits = 1;     // 2 banks
    const uint8_t  BANK0 = 0, BANK1 = 1;
    const uint16_t ROW0 = 0, ROW1 = 1, ROW2 = 2;

    const uint8_t  bytemask   = 0xFF;
    const uint64_t colmask    = 0xFF;     // 8 sub-columns × 8B
    const uint32_t bank0_mask = 1u << BANK0;
    const uint32_t bank1_mask = 1u << BANK1;

    CimModule cim;

    uint8_t a0[64], b0[64], a1[64], b1[64];
    gen_bank0_A(a0); gen_bank0_B(b0);
    gen_bank1_A(a1); gen_bank1_B(b1);

    uint8_t row0_b0[64], row1_b0[64], row0_b1[64], row1_b1[64];
    pack_planes64(a0, row0_b0);
    pack_planes64(b0, row1_b0);
    pack_planes64(a1, row0_b1);
    pack_planes64(b1, row1_b1);

    uint8_t exp_b0_row2[64], exp_b1_row2[64];
    raw_xor_then_pack(a0, b0, exp_b0_row2);
    raw_xor_then_pack(a1, b1, exp_b1_row2);

    uint8_t zeros[64]; std::memset(zeros, 0x00, 64);

    // 清 row2
    cim.copy_to_cim(BANK0, ROW2, zeros, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANK1, ROW2, zeros, num_bank_bits, ROW_BYTES);

    // 寫入 row0/row1
    cim.copy_to_cim(BANK0, ROW0, row0_b0, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANK0, ROW1, row1_b0, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANK1, ROW0, row0_b1, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANK1, ROW1, row1_b1, num_bank_bits, ROW_BYTES);

    // 讀回確認 row0/row1
    uint8_t chk[64]; size_t first=0;
    uint8_t out_b0_r0[64], out_b0_r1[64], out_b1_r0[64], out_b1_r1[64];
    cim.copy_to_cpu(chk, BANK0, ROW0, num_bank_bits, ROW_BYTES);
    memcpy(out_b0_r0, chk, 64);
    size_t d = diff_bytes(chk, row0_b0, 64, &first);
    printf("[B0 row0] diff=%zu%s%s\n", d, d?" (mismatch)":" (OK)", d? "":"" );
    cim.copy_to_cpu(chk, BANK0, ROW1, num_bank_bits, ROW_BYTES);
    memcpy(out_b0_r1, chk, 64);
    d = diff_bytes(chk, row1_b0, 64, &first);
    printf("[B0 row1] diff=%zu%s\n", d, d?" (mismatch)":" (OK)");
    cim.copy_to_cpu(chk, BANK1, ROW0, num_bank_bits, ROW_BYTES);
    memcpy(out_b1_r0, chk, 64);
    d = diff_bytes(chk, row0_b1, 64, &first);
    printf("[B1 row0] diff=%zu%s\n", d, d?" (mismatch)":" (OK)");
    cim.copy_to_cpu(chk, BANK1, ROW1, num_bank_bits, ROW_BYTES);
    memcpy(out_b1_r1, chk, 64);
    d = diff_bytes(chk, row1_b1, 64, &first);
    printf("[B1 row1] diff=%zu%s\n", d, d?" (mismatch)":" (OK)");
    hexdump16("[B0 row0]", out_b0_r0);
    hexdump16("[B0 row1]", out_b0_r1);
    hexdump16("[B1 row0]", out_b1_r0);
    hexdump16("[B1 row1]", out_b1_r1);

    // BANK0: XOR row0,row1 -> TEMP(row2), 再 materialize 回 RW(row2) 只限 bank0
    cim.XOR({uint8_t(ROW0), uint8_t(ROW1)}, bytemask, bank0_mask, colmask, uint8_t(ROW2));
    cim.COPY(uint16_t(ROW2), uint16_t(0x100u | ROW2), 0, bytemask, bank0_mask, colmask);

    // // BANK1: XOR row0,row1 -> TEMP(row2), 再 materialize 回 RW(row2) 只限 bank1
    cim.XOR({uint8_t(ROW0), uint8_t(ROW1)}, bytemask, bank1_mask, colmask, uint8_t(ROW2));
    cim.COPY(uint16_t(ROW2), uint16_t(0x100u | ROW2), 0, bytemask, bank1_mask, colmask);

    // 讀回 row2 比對期望
    uint8_t out_b0_r2[64], out_b1_r2[64];
    cim.copy_to_cpu(out_b0_r2, BANK0, ROW2, num_bank_bits, ROW_BYTES);
    cim.copy_to_cpu(out_b1_r2, BANK1, ROW2, num_bank_bits, ROW_BYTES);

    size_t idx0=0, idx1=0;
    size_t diff0 = diff_bytes(out_b0_r2, exp_b0_row2, 64, &idx0);
    size_t diff1 = diff_bytes(out_b1_r2, exp_b1_row2, 64, &idx1);

    hexdump16("[B0 row2 OUT]", out_b0_r2);
    hexdump16("[B0 row2 EXP]", exp_b0_row2);
    hexdump16("[B1 row2 OUT]", out_b1_r2);
    hexdump16("[B1 row2 EXP]", exp_b1_row2);

    printf("[XOR result] B0 diff=%zu%s", diff0, diff0?" (mismatch)":" (OK)");
    if (diff0) printf("  first@%zu", idx0);
    printf("  |  B1 diff=%zu%s", diff1, diff1?" (mismatch)":" (OK)");
    if (diff1) printf("  first@%zu", idx1);
    printf("\n");

    if (diff0 || diff1) {
        printf("ERROR: XOR result mismatch.\n");
        return 2;
    }

    printf("OK: Two-bank XOR on bit-packed rows matches raw(A^B) then pack.\n");
    return 0;
}