#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include "cim_api.hpp"

// Tiny helper to dump a few bytes
static void dump_hex(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        std::printf("%02X%s", p[i], (i + 1 == n) ? "\n" : " ");
    }
}

int main() {
    CimModule cim; // uses DEFAULT_* addresses: 0x10000000 (RW), 0x12000000 (CMD)

    // Two 64-byte rows for a quick demo (you can use DEFAULT_ROW_SIZE_BYTE too)
    const size_t N = 64;
    std::vector<uint8_t> a(N), b(N), out(N);

    // Fill test patterns
    for (size_t i = 0; i < N; ++i) {
        a[i] = (uint8_t)(0xF0 ^ (i & 0x0F));   // 0xF0.. pattern
        b[i] = (uint8_t)(0x0F ^ ((i * 3) & 0x0F));
    }

    // Copy to CIM rows: 0 and 1
    // Note: copy_to_cim copies exactly 'size_in_byte', so we pass N
    cim.copy_to_cim(/*row=*/0, a.data(), N);
    cim.copy_to_cim(/*row=*/1, b.data(), N);

    // Issue AND( row0, row1 ) -> dest row2
    // byte_mask=0xFF, bank_mask=all, column_mask=all, dest=2
    cim.AND(/*rows=*/{0, 1}, /*byte_mask=*/0xFF, /*bank_mask=*/0xFFFFFFFFu,
            /*column_mask=*/~0ULL, /*dest=*/2);

    // Read back result from row 2
    cim.copy_to_cpu(out.data(), /*row=*/2, N);

    // Print & check
    std::printf("A[0:16]: "); dump_hex(a.data(), 16);
    std::printf("B[0:16]: "); dump_hex(b.data(), 16);
    std::printf("AND[0:16]: "); dump_hex(out.data(), 16);

    bool ok = true;
    for (size_t i = 0; i < N; ++i) {
        if (out[i] != (uint8_t)(a[i] & b[i])) {
            ok = false; break;
        }
    }
    std::printf("Check: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}