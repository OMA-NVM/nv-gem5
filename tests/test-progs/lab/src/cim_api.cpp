// cim_api.cpp
#include "cim_api.hpp"
#include <cstddef>

static constexpr uint64_t kAllOnes64 = 0xffffffffffffffffull;

CimModule::CimModule(volatile uint64_t *const read_write_address,
                     volatile uint64_t *const command_address)
    : readWriteAddress(read_write_address),
      commandWriteAddress(command_address),
      bankBits(0),
      columnBits(0)
{
}

void CimModule::setGeometry(unsigned num_bank_bits, unsigned num_column_bits)
{
    bankBits = num_bank_bits;
    columnBits = num_column_bits;

    assert(bankBits > 0);
    assert(bankBits <= 64);
    assert(columnBits > 0);
    assert(columnBits <= 63);
}

uint64_t CimModule::Mask::bank(uint8_t b)
{
    return (b < 64) ? (1ull << b) : 0ull;
}

uint64_t CimModule::Mask::banksAll(unsigned bank_bits)
{
    if (bank_bits >= 64) return kAllOnes64;
    return (bank_bits == 0) ? 0ull : ((1ull << bank_bits) - 1ull);
}

uint64_t CimModule::Mask::colsAll()
{
    return kAllOnes64;
}

void CimModule::checkGeometryReady() const
{
    assert(bankBits > 0);
    assert(bankBits <= 64);
    assert(columnBits > 0);
    assert(columnBits <= 63);
}

uint64_t CimModule::normalizeBankMask(uint64_t bank_mask) const
{
    checkGeometryReady();

    const uint64_t valid = Mask::banksAll(bankBits);

    if (bank_mask == kAllOnes64) {
        return valid;
    }

    assert((bank_mask & ~valid) == 0);
    return bank_mask;
}

CimModule::CommandEncode::CommandEncode(volatile uint64_t *const command_address)
    : commandAddress(command_address)
{
    operation_type = 0xff;
    operation_flag_mask = 0;
    byte_mask = 0xffu;
    bank_mask = kAllOnes64;
    column_mask = kAllOnes64;
    dest = 0;

    for (int i = 0; i < 8; ++i) {
        row_number[i] = 0xffffu;
    }
}

void CimModule::generateCommand(uint8_t op_type,
                               const std::vector<uint8_t> &rows,
                               uint8_t byte_mask,
                               uint64_t bank_mask,
                               uint64_t column_mask,
                               uint8_t dest)
{
    checkGeometryReady();

    assert(rows.size() >= 2);
    assert(rows.size() <= 8);
    assert(byte_mask != 0);
    assert(column_mask != 0);

    CommandEncode c1(this->commandWriteAddress);

    c1.operation_type = op_type;
    c1.byte_mask = byte_mask;
    c1.bank_mask = normalizeBankMask(bank_mask);
    c1.column_mask = column_mask;
    c1.dest = dest;

    for (size_t i = 0; i < rows.size(); i++) {
        c1.row_number[i] = rows[i];
        c1.operation_flag_mask |= static_cast<uint8_t>(1u << i);
    }

    c1.issue();
}

void CimModule::AND(const std::vector<uint8_t> &rows,
                    uint8_t byte_mask,
                    uint64_t bank_mask,
                    uint64_t column_mask,
                    uint8_t dest)
{
    generateCommand(0, rows, byte_mask, bank_mask, column_mask, dest);
}

void CimModule::OR(const std::vector<uint8_t> &rows,
                   uint8_t byte_mask,
                   uint64_t bank_mask,
                   uint64_t column_mask,
                   uint8_t dest)
{
    generateCommand(1, rows, byte_mask, bank_mask, column_mask, dest);
}

void CimModule::XOR(const std::vector<uint8_t> &rows,
                    uint8_t byte_mask,
                    uint64_t bank_mask,
                    uint64_t column_mask,
                    uint8_t dest)
{
    generateCommand(2, rows, byte_mask, bank_mask, column_mask, dest);
}

void CimModule::COPY(const uint16_t &dest,
                     const uint16_t &src,
                     const uint8_t &rotate_left,
                     const uint8_t &byte_mask,
                     const uint64_t &bank_mask,
                     const uint64_t &column_mask)
{
    checkGeometryReady();

    assert(byte_mask != 0);
    assert(column_mask != 0);

    CommandEncode c1(this->commandWriteAddress);

    c1.operation_type = 3;
    c1.byte_mask = byte_mask;
    c1.bank_mask = normalizeBankMask(bank_mask);
    c1.column_mask = column_mask;
    c1.dest = dest;
    c1.row_number[0] = src;
    c1.operation_flag_mask = rotate_left;

    c1.issue();
}

void CimModule::NOT_COND(const uint16_t &dest,
                         const uint16_t &src,
                         const bool &always_NOT,
                         const bool &NOT_if_zero,
                         const uint8_t &byte_mask,
                         const uint64_t &bank_mask,
                         const uint64_t &column_mask)
{
    checkGeometryReady();

    assert(byte_mask != 0);
    assert(column_mask != 0);

    CommandEncode c1(this->commandWriteAddress);

    c1.operation_type = 4;
    c1.byte_mask = byte_mask;
    c1.bank_mask = normalizeBankMask(bank_mask);
    c1.column_mask = column_mask;
    c1.dest = dest;
    c1.row_number[0] = src;

    if (always_NOT) {
        c1.operation_flag_mask = static_cast<uint8_t>(c1.operation_flag_mask + 2);
    }
    if (NOT_if_zero) {
        c1.operation_flag_mask = static_cast<uint8_t>(c1.operation_flag_mask + 1);
    }

    c1.issue();
}

void CimModule::copy_to_cim(uint8_t bank,
                            uint16_t row,
                            void *cpu_array,
                            size_t size_in_byte)
{
    checkGeometryReady();

    assert(cpu_array != nullptr);
    assert(readWriteAddress != nullptr);
    assert(bank < (1u << bankBits));
    assert(size_in_byte == (1ull << columnBits));

    const uintptr_t base = reinterpret_cast<uintptr_t>(readWriteAddress);
    const uintptr_t offs =
        (static_cast<uintptr_t>(row)  << (bankBits + columnBits)) +
        (static_cast<uintptr_t>(bank) <<  columnBits);

    uint8_t *dest = reinterpret_cast<uint8_t *>(base + offs);
    std::memcpy(dest, cpu_array, size_in_byte);
}

void CimModule::copy_to_cpu(void *cpu_array,
                            uint8_t bank,
                            uint16_t row,
                            size_t size_in_byte)
{
    checkGeometryReady();

    assert(cpu_array != nullptr);
    assert(readWriteAddress != nullptr);
    assert(bank < (1u << bankBits));
    assert(size_in_byte == (1ull << columnBits));

    const uintptr_t base = reinterpret_cast<uintptr_t>(readWriteAddress);
    const uintptr_t offs =
        (static_cast<uintptr_t>(row)  << (bankBits + columnBits)) +
        (static_cast<uintptr_t>(bank) <<  columnBits);

    const uint8_t *src = reinterpret_cast<const uint8_t *>(base + offs);
    std::memcpy(cpu_array, src, size_in_byte);
}

void CimModule::CommandEncode::print()
{
    printf("-------\n** Printing command:\n");
    printf("type: %02x, flag: %02x, byte_mask: %02x\n",
           operation_type, operation_flag_mask, byte_mask);
    printf("bank_mask: 0x%016" PRIx64 ", column_mask: 0x%016" PRIx64 "\n",
           bank_mask, column_mask);
    printf("row: %04x\n", row_number[0]);
    printf("row: %04x\n", row_number[1]);
    printf("row: %04x\n", row_number[2]);
    printf("row: %04x\n", row_number[3]);
    printf("row: %04x\n", row_number[4]);
    printf("row: %04x\n", row_number[5]);
    printf("row: %04x\n", row_number[6]);
    printf("row: %04x\n", row_number[7]);
    printf("dest: %04x\n------\n", dest);
}

void CimModule::CommandEncode::issue()
{
    uint8_t row_counter = 0;
    for (auto r : row_number) {
        if (r < 256) row_counter++;
    }

    if ((row_counter <= 4) &&
        (bank_mask == kAllOnes64) &&
        (column_mask == kAllOnes64))
    {
        uint64_t command_to_send = 0;
        command_to_send |= (static_cast<uint64_t>(operation_type | 0x80u)) << (8 * 7);
        command_to_send |= (static_cast<uint64_t>(operation_flag_mask))     << (8 * 6);
        command_to_send |= (static_cast<uint64_t>(byte_mask))               << (8 * 4);

        if ((operation_type % 0x80u) < 3) {
            command_to_send |= (static_cast<uint64_t>(dest)) << (8 * 5);
            for (size_t i = 0; i < row_counter; i++) {
                command_to_send |= (static_cast<uint64_t>(row_number[i] & 0xffu)) << (8 * i);
            }
        } else {
            command_to_send |= (static_cast<uint64_t>(dest)) << (8 * 2);
            command_to_send |= (static_cast<uint64_t>(row_number[0] & 0xffffu));
        }

        *commandAddress = command_to_send;
        return;
    }

    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;

    w0 |= (static_cast<uint64_t>(operation_type))      << (8 * 7);
    w0 |= (static_cast<uint64_t>(operation_flag_mask)) << (8 * 6);
    w0 |= (static_cast<uint64_t>(byte_mask))           << (8 * 4);

    const uint64_t bm_lo = static_cast<uint64_t>(bank_mask & 0xffffffffull);
    const uint64_t bm_hi = static_cast<uint64_t>((bank_mask >> 32) & 0xffffffffull);
    w0 |= bm_lo;
    w3  = bm_hi;

    w1 = column_mask;

    if ((operation_type % 0x80u) < 3) {
        w0 |= (static_cast<uint64_t>(dest)) << (8 * 5);
        for (size_t i = 0; i < row_counter; i++) {
            w2 |= (static_cast<uint64_t>(row_number[i] & 0xffu)) << (8 * i);
        }
    } else {
        w2 |= (static_cast<uint64_t>(dest)) << 16;
        w2 |= (static_cast<uint64_t>(row_number[0] & 0xffffu));
    }

    volatile uint64_t *ca = commandAddress;
    ca[1] = w1;
    ca[2] = w2;
    ca[3] = w3;
    __sync_synchronize();
    ca[0] = w0;
}