#include <stdio.h>
#include <stdint.h>

// Tiny helper to dump a few bytes
static void dump_hex(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        printf("%02X%s", p[i], (i + 1 == n) ? "\n" : " ");
    }
}

int main() {
    printf("Starting simple C test...\n");
    
    // 測試基本的記憶體操作 - 使用陣列而非 vector
    uint8_t test_data[64];
    for (size_t i = 0; i < 64; ++i) {
        test_data[i] = (uint8_t)(i & 0xFF);
    }
    
    printf("Memory test passed\n");
    
    // 測試基本的位運算
    uint8_t a = 0xF0;
    uint8_t b = 0x0F;
    uint8_t result = a & b;
    
    printf("Bitwise operation: 0x%02X & 0x%02X = 0x%02X\n", a, b, result);
    
    // 測試陣列輸出
    printf("First 16 bytes: ");
    dump_hex(test_data, 16);
    
    printf("Simple C test completed successfully\n");
    
    return 0;
}