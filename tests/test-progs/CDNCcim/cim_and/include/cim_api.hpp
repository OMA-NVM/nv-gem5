#ifndef __CIM_API__HPP__
#define __CIM_API__HPP__

#include <cinttypes>
#include <iostream>
#include <vector>

#include <cassert>
#include <cstring>

#define DEFAULT_READWRITE_ADDRESS ((volatile uint64_t *const)0x10000000ul)
#define DEFAULT_COMMAND_ADDRESS ((volatile uint64_t *const)0x12000000ul)
#define DEFAULT_ROW_SIZE_BYTE (0x2000u) //= 16*64*8 = 16 * 512 --> 8 KiB

class CimModule
{
  protected:
    volatile uint64_t *const readWriteAddress;
    volatile uint64_t *const commandWriteAddress;
    struct CommandEncode
    {
      private:
        volatile uint64_t *const commandAddress;

      public:
        uint8_t operation_type { 0xff };
        uint8_t operation_flag_mask { 0 };
        //
        uint8_t byte_mask { 0xffu };
        uint32_t bank_mask { 0xfffffffu };
        uint64_t column_mask { 0xfffffffffffffffful };
        //
        uint16_t row_number[8] { 0xffffu, 0xffffu, 0xffffu, 0xffffu,
                                 0xffffu, 0xffffu, 0xffffu, 0xffffu };
        //
        uint16_t dest { 0 };
        //
        CommandEncode(volatile uint64_t *const command_address)
            : commandAddress(command_address)
        {
        }
        void print();
        void issue();
    };

    void generateCommand(
        const uint8_t op_type, const std::vector<uint8_t> &rows,
        const uint8_t &byte_mask = 0xffu,
        const uint32_t &bank_mask = 0xffffffffu,
        const uint64_t &column_mask = 0xfffffffffffffffful,
        const uint8_t &dest = 0x00u);

  public:
    CimModule(
        volatile uint64_t *const read_write_address
        = DEFAULT_READWRITE_ADDRESS,
        volatile uint64_t *const command_address = DEFAULT_COMMAND_ADDRESS)
        : readWriteAddress(read_write_address),
          commandWriteAddress(command_address)
    {
    }

    void AND(
        const std::vector<uint8_t> &rows, const uint8_t &byte_mask = 0xffu,
        const uint32_t &bank_mask = 0xffffffffu,
        const uint64_t &column_mask = 0xfffffffffffffffful,
        const uint8_t &dest = 0x00u);
    void OR(
        const std::vector<uint8_t> &rows, const uint8_t &byte_mask = 0xffu,
        const uint32_t &bank_mask = 0xffffffffu,
        const uint64_t &column_mask = 0xfffffffffffffffful,
        const uint8_t &dest = 0x00u);
    void XOR(
        const std::vector<uint8_t> &rows, const uint8_t &byte_mask = 0xffu,
        const uint32_t &bank_mask = 0xffffffffu,
        const uint64_t &column_mask = 0xfffffffffffffffful,
        const uint8_t &dest = 0x00u);

    void COPY(
        const uint16_t &dest, const uint16_t &src = 0x100,
        const uint8_t &rotate_left = 0, const uint8_t &byte_mask = 0xffu,
        const uint32_t &bank_mask = 0xffffffffu,
        const uint64_t &column_mask = 0xfffffffffffffffful);
    void NOT_COND(
        const uint16_t &dest, const uint16_t &src, const bool &always_NOT,
        const bool &NOT_if_zero, const uint8_t &byte_mask = 0xffu,
        const uint32_t &bank_mask = 0xffffffffu,
        const uint64_t &column_mask = 0xfffffffffffffffful);

    void copy_to_cim(
        const uint16_t &row, void *cpu_array,
        size_t size_in_byte = DEFAULT_ROW_SIZE_BYTE);
    void copy_to_cpu(
        void *cpu_array, const uint16_t &row,
        size_t size_in_byte = DEFAULT_ROW_SIZE_BYTE);
};

#endif //__CIM_API__HPP__
