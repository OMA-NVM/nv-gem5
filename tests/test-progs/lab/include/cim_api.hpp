// cim_api.hpp
#ifndef __CIM_API__HPP__
#define __CIM_API__HPP__

#include <cinttypes>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>

#define DEFAULT_READWRITE_ADDRESS ((volatile uint64_t *const)0x10000000ul)
#define DEFAULT_COMMAND_ADDRESS   ((volatile uint64_t *const)0x12000000ul)
#define DEFAULT_ROW_SIZE_BYTE     (0x40u)

class CimModule
{
  protected:
    volatile uint64_t *const readWriteAddress;
    volatile uint64_t *const commandWriteAddress;

    unsigned bankBits;
    unsigned columnBits;

    struct CommandEncode
    {
      private:
        volatile uint64_t *const commandAddress;

      public:
        uint8_t  operation_type;
        uint8_t  operation_flag_mask;
        uint8_t  byte_mask;
        uint64_t bank_mask;
        uint64_t column_mask;
        uint16_t row_number[8];
        uint16_t dest;

        CommandEncode(volatile uint64_t *const command_address);

        void print();
        void issue();
    };

    uint64_t normalizeBankMask(uint64_t bank_mask) const;
    void checkGeometryReady() const;

    void generateCommand(
        uint8_t op_type,
        const std::vector<uint8_t> &rows,
        uint8_t byte_mask = 0xffu,
        uint64_t bank_mask = 0xffffffffffffffffull,
        uint64_t column_mask = 0xffffffffffffffffull,
        uint8_t dest = 0x00u);

  public:
    struct Mask
    {
        static uint64_t bank(uint8_t b);
        static uint64_t banksAll(unsigned bank_bits);
        static uint64_t colsAll();
    };

    CimModule(
        volatile uint64_t *const read_write_address = DEFAULT_READWRITE_ADDRESS,
        volatile uint64_t *const command_address = DEFAULT_COMMAND_ADDRESS);

    void setGeometry(unsigned num_bank_bits, unsigned num_column_bits);

    void AND(
        const std::vector<uint8_t> &rows,
        uint8_t byte_mask = 0xffu,
        uint64_t bank_mask = 0xffffffffffffffffull,
        uint64_t column_mask = 0xffffffffffffffffull,
        uint8_t dest = 0x00u);

    void OR(
        const std::vector<uint8_t> &rows,
        uint8_t byte_mask = 0xffu,
        uint64_t bank_mask = 0xffffffffffffffffull,
        uint64_t column_mask = 0xffffffffffffffffull,
        uint8_t dest = 0x00u);

    void XOR(
        const std::vector<uint8_t> &rows,
        uint8_t byte_mask = 0xffu,
        uint64_t bank_mask = 0xffffffffffffffffull,
        uint64_t column_mask = 0xffffffffffffffffull,
        uint8_t dest = 0x00u);

    void COPY(
        const uint16_t &dest,
        const uint16_t &src = 0x100,
        const uint8_t &rotate_left = 0,
        const uint8_t &byte_mask = 0xffu,
        const uint64_t &bank_mask = 0xffffffffffffffffull,
        const uint64_t &column_mask = 0xffffffffffffffffull);

    void NOT_COND(
        const uint16_t &dest,
        const uint16_t &src,
        const bool &always_NOT,
        const bool &NOT_if_zero,
        const uint8_t &byte_mask = 0xffu,
        const uint64_t &bank_mask = 0xffffffffffffffffull,
        const uint64_t &column_mask = 0xffffffffffffffffull);

    void copy_to_cim(
        uint8_t bank,
        uint16_t row,
        void *cpu_array,
        size_t size_in_byte);

    void copy_to_cpu(
        void *cpu_array,
        uint8_t bank,
        uint16_t row,
        size_t size_in_byte);
};

#endif // __CIM_API__HPP__