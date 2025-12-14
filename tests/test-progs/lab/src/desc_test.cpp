#include "cim_api.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

#if __has_include(<gem5/m5ops.h>)
  #include <gem5/m5ops.h>
  #define HAS_M5OPS 1
#elif __has_include(<m5ops.h>)
  #include <m5ops.h>
  #define HAS_M5OPS 1
#else
  #define HAS_M5OPS 0
#endif

using namespace std;

static constexpr uint8_t  BYTEMASK_ALL = 0xFFu;
static constexpr uint64_t COLMASK_ALL  = 0xFFFFFFFFFFFFFFFFull;

// Your current gem5 config:
// banks_per_rank = 32  => bank bits = 5
// num_column_bits = 6  => 64B stride per bank
static constexpr unsigned NUM_BANK_BITS   = 5;
static constexpr unsigned NUM_COLUMN_BITS = 6;

static bool check_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    return std::memcmp(a, b, n) == 0;
}

static void dump_bytes(const char *name, const uint8_t *got,
                       const uint8_t *exp, size_t n = 16)
{
    std::cout << name << " (got vs expected):\n  ";
    for (size_t i = 0; i < n; ++i) {
        std::cout << std::hex << std::uppercase << "0x"
                  << (int)got[i] << " ";
    }
    std::cout << "\n  ";
    for (size_t i = 0; i < n; ++i) {
        std::cout << std::hex << std::uppercase << "0x"
                  << (int)exp[i] << " ";
    }
    std::cout << std::dec << "\n";
}

// Simple deterministic PRNG for better test patterns
static inline uint32_t xorshift32(uint32_t &s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int main()
{
    constexpr size_t ROW_BYTES = DEFAULT_ROW_SIZE_BYTE;

    uint8_t rowA[ROW_BYTES];
    uint8_t rowB[ROW_BYTES];
    uint8_t exp_or[ROW_BYTES];
    uint8_t exp_and[ROW_BYTES];
    uint8_t exp_xor[ROW_BYTES];

    // Use non-tricky, non-complementary, deterministic patterns
    for (size_t i = 0; i < ROW_BYTES; ++i) {
        uint32_t sa = 0x12345678u ^ (uint32_t)i;
        uint32_t sb = 0x9E3779B9u ^ (uint32_t)(i * 17u + 3u);

        uint8_t a = (uint8_t)(xorshift32(sa) & 0xFFu);
        uint8_t b = (uint8_t)(xorshift32(sb) & 0xFFu);

        rowA[i] = a;
        rowB[i] = b;

        exp_or[i]  = (uint8_t)(a | b);
        exp_and[i] = (uint8_t)(a & b);
        exp_xor[i] = (uint8_t)(a ^ b);
    }

    const uint16_t ROW_A   = 0;
    const uint16_t ROW_B   = 1;
    const uint16_t ROW_OR  = 2;
    const uint16_t ROW_AND = 3;
    const uint16_t ROW_XOR = 4;

    const uint8_t BANK0 = 0;

    CimModule cim;

    // Set geometry once for the module
    cim.setGeometry(NUM_BANK_BITS, NUM_COLUMN_BITS);

    const uint64_t BANKMASK_B0 = CimModule::Mask::bank(BANK0);

    uint8_t zeros[ROW_BYTES];
    std::memset(zeros, 0, ROW_BYTES);

    // =========================
    // Offline preload (不計時)
    // =========================

    // Clear rows in bank0
    cim.copy_to_cim(BANK0, ROW_A,   zeros, ROW_BYTES);
    cim.copy_to_cim(BANK0, ROW_B,   zeros, ROW_BYTES);
    cim.copy_to_cim(BANK0, ROW_OR,  zeros, ROW_BYTES);
    cim.copy_to_cim(BANK0, ROW_AND, zeros, ROW_BYTES);
    cim.copy_to_cim(BANK0, ROW_XOR, zeros, ROW_BYTES);

    // Write A/B into bank0
    cim.copy_to_cim(BANK0, ROW_A, rowA, ROW_BYTES);
    cim.copy_to_cim(BANK0, ROW_B, rowB, ROW_BYTES);

    // =========================
    // ROI begins: bitwise + readback
    // =========================
#if HAS_M5OPS
    m5_work_begin(0, 0);
#endif

    // OR: result -> buffer(0x100|ROW_OR), then COPY back to ROW_OR
    cim.OR({(uint8_t)ROW_A, (uint8_t)ROW_B}, BYTEMASK_ALL, BANKMASK_B0, COLMASK_ALL, (uint8_t)ROW_OR);
    cim.COPY(
        (uint16_t)ROW_OR,
        (uint16_t)(0x100u | ROW_OR),
        0,
        BYTEMASK_ALL,
        BANKMASK_B0,
        COLMASK_ALL
    );

    // AND: result -> buffer(0x100|ROW_AND), then COPY back to ROW_AND
    cim.AND({(uint8_t)ROW_A, (uint8_t)ROW_B}, BYTEMASK_ALL, BANKMASK_B0, COLMASK_ALL, (uint8_t)ROW_AND);
    cim.COPY(
        (uint16_t)ROW_AND,
        (uint16_t)(0x100u | ROW_AND),
        0,
        BYTEMASK_ALL,
        BANKMASK_B0,
        COLMASK_ALL
    );

    // XOR: result -> buffer(0x100|ROW_XOR), then COPY back to ROW_XOR
    cim.XOR({(uint8_t)ROW_A, (uint8_t)ROW_B}, BYTEMASK_ALL, BANKMASK_B0, COLMASK_ALL, (uint8_t)ROW_XOR);
    cim.COPY(
        (uint16_t)ROW_XOR,
        (uint16_t)(0x100u | ROW_XOR),
        0,
        BYTEMASK_ALL,
        BANKMASK_B0,
        COLMASK_ALL
    );

    uint8_t got_or[ROW_BYTES];
    uint8_t got_and[ROW_BYTES];
    uint8_t got_xor[ROW_BYTES];

    std::memset(got_or,  0, ROW_BYTES);
    std::memset(got_and, 0, ROW_BYTES);
    std::memset(got_xor, 0, ROW_BYTES);

    cim.copy_to_cpu(got_or,  BANK0, ROW_OR,  ROW_BYTES);
    cim.copy_to_cpu(got_and, BANK0, ROW_AND, ROW_BYTES);
    cim.copy_to_cpu(got_xor, BANK0, ROW_XOR, ROW_BYTES);

#if HAS_M5OPS
    m5_work_end(0, 0);
#endif
    // =========================
    // ROI ends
    // =========================

    bool ok_or  = check_equal(got_or,  exp_or,  ROW_BYTES);
    bool ok_and = check_equal(got_and, exp_and, ROW_BYTES);
    bool ok_xor = check_equal(got_xor, exp_xor, ROW_BYTES);

    std::cout << "CIM bitwise test (OR/AND/XOR) [bank0 only]:\n";
    std::cout << "  OR (A|B)   : " << (ok_or  ? "PASS" : "FAIL") << "\n";
    std::cout << "  AND(A&B)   : " << (ok_and ? "PASS" : "FAIL") << "\n";
    std::cout << "  XOR(A^B)   : " << (ok_xor ? "PASS" : "FAIL") << "\n";

    // === Always print got vs expected ===
    static constexpr size_t PRINT_N = 64; 
    std::cout << "\n[Dump first " << PRINT_N << " bytes] (got vs expected)\n";
    dump_bytes("OR ",  got_or,  exp_or,  PRINT_N);
    dump_bytes("AND",  got_and, exp_and, PRINT_N);
    dump_bytes("XOR",  got_xor, exp_xor, PRINT_N);

    // (Optional) extra hints
    if (!ok_or)  std::cout << "  NOTE: OR mismatch detected.\n";
    if (!ok_and) std::cout << "  NOTE: AND mismatch detected.\n";
    if (!ok_xor) std::cout << "  NOTE: XOR mismatch detected.\n";

    return (ok_or && ok_and && ok_xor) ? 0 : 1;
}