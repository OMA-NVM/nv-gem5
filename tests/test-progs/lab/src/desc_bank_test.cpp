// bank63_xor_copy_smoketest.cpp
#include "cim_api.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace std;

// One row = 64B when num_column_bits=6 (8 sub-columns × 8B)
#define ROW_BYTES (DEFAULT_ROW_SIZE_BYTE / sizeof(uint8_t))
static_assert(ROW_BYTES == 64, "Expect 64 bytes per row when num_column_bits=6.");

static inline void fillA(uint8_t* d){ for (int i=0;i<64;++i) d[i]=uint8_t(0x10 + i); }
static inline void fillB(uint8_t* d){ for (int i=0;i<64;++i) d[i]=uint8_t(0xA5 ^ uint8_t(i*3)); }

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

int main() {
    // 64 banks: use only bank #63 here
    const int      num_bank_bits = 6;        // python 也要設成 6
    const uint8_t  BANKX = 63;
    const uint16_t ROW0  = 0, ROW1 = 1, ROW2 = 2;

    const uint8_t  bytemask  = 0xFF;         // enable 8 bits/byte
    const uint64_t colmask   = 0xFFull;      // 8 sub-columns × 8B
    const uint64_t bankmaskX = (1ull << BANKX);

    CimModule cim;

    // Host buffers
    uint8_t a[64], b[64], exp_xor[64], zeros[64];
    uint8_t out_r0[64], out_r1[64], out_r2[64];
    fillA(a); fillB(b);
    for (int i=0;i<64;++i) exp_xor[i] = uint8_t(a[i]^b[i]);
    memset(zeros, 0x00, 64);

    // Clear rows on bank63
    cim.copy_to_cim(BANKX, ROW0, zeros, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANKX, ROW1, zeros, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANKX, ROW2, zeros, num_bank_bits, ROW_BYTES);

    // Write inputs to bank63 row0/row1
    cim.copy_to_cim(BANKX, ROW0, a, num_bank_bits, ROW_BYTES);
    cim.copy_to_cim(BANKX, ROW1, b, num_bank_bits, ROW_BYTES);

    // Read back row0/row1 for a quick sanity preview
    cim.copy_to_cpu(out_r0, BANKX, ROW0, num_bank_bits, ROW_BYTES);
    cim.copy_to_cpu(out_r1, BANKX, ROW1, num_bank_bits, ROW_BYTES);
    hexdump16("[B63 row0]", out_r0);
    hexdump16("[B63 row1]", out_r1);

    // XOR(row0,row1) -> TEMP(row2), then COPY TEMP(row2) -> RW(row2)
    // 注意：COPY( src, dest, ... )；src/dest 的 0x100 標誌 TEMP
    cim.XOR({uint8_t(ROW0), uint8_t(ROW1)}, bytemask, bankmaskX, colmask, uint8_t(ROW2));
    cim.COPY(uint16_t(ROW2), uint16_t(0x100u | ROW2), 0, bytemask, bankmaskX, colmask);

    // Read back row2 and compare with CPU a^b
    cim.copy_to_cpu(out_r2, BANKX, ROW2, num_bank_bits, ROW_BYTES);

    hexdump16("[B63 row2 OUT]", out_r2);
    hexdump16("[EXP XOR    ]",  exp_xor);

    size_t first = 0;
    size_t d = diff_bytes(out_r2, exp_xor, 64, &first);
    printf("[XOR+COPY on bank63] diff=%zu%s", d, d?" (mismatch)":" (OK)");
    if (d) printf("  first@%zu  OUT=%02X EXP=%02X", first, out_r2[first], exp_xor[first]);
    printf("\n");

    if (d) {
        printf("ERROR: XOR+COPY on bank63 failed.\n");
        return 1;
    }

    printf("OK: XOR(row0,row1)->TEMP(row2) then COPY TEMP->RW(row2) works on bank 63.\n");
    return 0;
}