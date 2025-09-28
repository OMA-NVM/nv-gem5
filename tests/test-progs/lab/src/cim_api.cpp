#include "cim_api.hpp"

void
CimModule::generateCommand(
    const uint8_t op_type, const std::vector<uint8_t> &rows,
    const uint8_t &byte_mask, const uint32_t &bank_mask,
    const uint64_t &column_mask, const uint8_t &dest)
{
    assert((rows.size() >= 2) || (rows.size() <= 8));

    CommandEncode c1(this->commandWriteAddress);

    c1.operation_type = op_type;
    c1.byte_mask = byte_mask;
    c1.bank_mask = bank_mask;
    c1.column_mask = column_mask;
    c1.dest = dest;
    for (size_t i = 0; i < rows.size(); i++)
    {
        c1.row_number[i] = rows[i];
        c1.operation_flag_mask |= (1u << i);
    }

    // c1.print();
    c1.issue();
}

void
CimModule::AND(
    const std::vector<uint8_t> &rows, const uint8_t &byte_mask,
    const uint32_t &bank_mask, const uint64_t &column_mask,
    const uint8_t &dest)
{
    generateCommand(0, rows, byte_mask, bank_mask, column_mask, dest);
}

void
CimModule::OR(
    const std::vector<uint8_t> &rows, const uint8_t &byte_mask,
    const uint32_t &bank_mask, const uint64_t &column_mask,
    const uint8_t &dest)
{
    generateCommand(1, rows, byte_mask, bank_mask, column_mask, dest);
}

void
CimModule::XOR(
    const std::vector<uint8_t> &rows, const uint8_t &byte_mask,
    const uint32_t &bank_mask, const uint64_t &column_mask,
    const uint8_t &dest)
{
    generateCommand(2, rows, byte_mask, bank_mask, column_mask, dest);
}

void
CimModule::COPY(
    const uint16_t &dest, const uint16_t &src, const uint8_t &rotate_left,
    const uint8_t &byte_mask, const uint32_t &bank_mask,
    const uint64_t &column_mask)
{
    CommandEncode c1(this->commandWriteAddress);

    c1.operation_type = 3;
    c1.byte_mask = byte_mask;
    c1.bank_mask = bank_mask;
    c1.column_mask = column_mask;
    c1.dest = dest;
    c1.row_number[0] = src;
    c1.operation_flag_mask = rotate_left;

    // c1.print();
    c1.issue();
}

void
CimModule::NOT_COND(
    const uint16_t &dest, const uint16_t &src, const bool &always_NOT,
    const bool &NOT_if_zero, const uint8_t &byte_mask,
    const uint32_t &bank_mask, const uint64_t &column_mask)
{
    CommandEncode c1(this->commandWriteAddress);

    c1.operation_type = 4;
    c1.byte_mask = byte_mask;
    c1.bank_mask = bank_mask;
    c1.column_mask = column_mask;
    c1.dest = dest;
    c1.row_number[0] = src;
    if (always_NOT) {
        c1.operation_flag_mask += static_cast<uint8_t>(2);
    }

    if (NOT_if_zero) {
        c1.operation_flag_mask += static_cast<uint8_t>(1);
    }

    // c1.print();
    c1.issue();
}

void
CimModule::copy_to_cim(
    const uint16_t &row, void *cpu_array, size_t size_in_byte)
{
    void *dest = (void *)(readWriteAddress + (row * (DEFAULT_ROW_SIZE_BYTE >> 3)));
    std::memcpy(
        (void *)(readWriteAddress + (row * DEFAULT_ROW_SIZE_BYTE >> 3)),
        cpu_array, size_in_byte);

    // printf("[CIM copy_to_cim] row=%u, size=%zu, readWriteAddress=%p, dest=%p\n",
    //        row, size_in_byte, (void*)readWriteAddress, dest);
}

void
CimModule::copy_to_cpu(
    void *cpu_array, const uint16_t &row, size_t size_in_byte)
{
    void *dest = (void *)(readWriteAddress + (row * (DEFAULT_ROW_SIZE_BYTE >> 3)));
    std::memcpy(
        cpu_array,
        (void *)(readWriteAddress + (row * DEFAULT_ROW_SIZE_BYTE >> 3)),
        size_in_byte);

    // printf("[CIM copy_to_cpu] row=%u, size=%zu, readWriteAddress=%p, dest=%p\n",
    //        row, size_in_byte, (void*)readWriteAddress, dest);
}

void
CimModule::CommandEncode::print()
{
    printf("-------\n** Printing command: \n");
    printf(
        "type: %02x, flag: %02x, byte_mask: %02x\n", operation_type,
        operation_flag_mask, byte_mask);
    printf("bank_mask: %08x , column_mask: %016llx\n", bank_mask, column_mask);
    printf("row: %04x \n", row_number[0]);
    printf("row: %04x \n", row_number[1]);
    printf("row: %04x \n", row_number[2]);
    printf("row: %04x \n", row_number[3]);
    printf("row: %04x \n", row_number[4]);
    printf("row: %04x \n", row_number[5]);
    printf("row: %04x \n", row_number[6]);
    printf("row: %04x \n", row_number[7]);
    printf("dest: %04x \n------\n", dest);
    return;
}

void
CimModule::CommandEncode::issue()
{
    uint8_t row_counter = 0;
    for (auto row : row_number)
    {
        if (row < 256)
            row_counter++;
    }

    // Check for short Command
    if ((row_counter <= 4) && (bank_mask == 0xffffffffu)
        && (column_mask == 0xfffffffffffffffful))
    {
        uint64_t command_to_send = 0;
        //
        command_to_send |= ((uint64_t)(operation_type | 0x80u)) << (8 * 7);
        command_to_send |= ((uint64_t)operation_flag_mask) << (8 * 6);
        command_to_send |= ((uint64_t)byte_mask) << (8 * 4);
        //
        if ((operation_type % 0x80) < 3) // and or xor:
        {
            command_to_send |= ((uint64_t)dest) << (8 * 5);
            for (size_t i = 0; i < row_counter; i++)
            {
                command_to_send |= ((uint64_t)(row_number[i] & 0xffu))
                                   << (8 * i);
            }
        }
        else
        {
            command_to_send |= ((uint64_t)dest) << (8 * 2);
            command_to_send |= ((uint64_t)(row_number[0] & 0xffffu));
        }

        // === DEBUG: dump encoded command just written ===
        // {
        //     volatile uint64_t *cmd = &command_to_send;
        //     uint64_t w0 = cmd[0], w1 = cmd[1], w2 = cmd[2];

        //     printf("[CIM ISSUE][RAW] @%p  w0=%016llx w1=%016llx w2=%016llx\n",
        //         (void*)cmd,
        //         (unsigned long long)w0,
        //         (unsigned long long)w1,
        //         (unsigned long long)w2);

        //     if (w0 == 0 && w1 == 0 && w2 == 0) {
        //         printf("[CIM ISSUE] commandAddress is all zero (maybe fetched/cleared already)\n");
        //     } else if (w0 & (1ull << 63)) {
        //         // Short instruction
        //         uint8_t  op     = (w0 >> 56) & 0xff;
        //         uint8_t  base   = op & 0x7f;             // 0x00.. for AND/OR/XOR/COPY/NOT_COND
        //         uint8_t  flags  = (w0 >> 48) & 0xff;
        //         uint8_t  bytem  = (w0 >> 32) & 0xff;
        //         printf("[CIM ISSUE][SHORT] op=0x%02x (base=0x%02x) flags=0x%02x byte_mask=0x%02x\n",
        //             op, base, flags, bytem);

        //         if (base < 3) {
        //             // SHORT_LOGIC: AND/OR/XOR
        //             uint8_t dest8 = (w0 >> 40) & 0xff;
        //             printf("  logic: dest(8b)=0x%02x rows(0..3) bytes=%08x\n",
        //                 dest8, (unsigned)((uint32_t)(w0 & 0xffffffffu)));
        //         } else {
        //             // SHORT_COPY / SHORT_NOT_COND
        //             uint16_t dest16 = (w0 >> 16) & 0xffff;
        //             uint16_t src16  =  w0        & 0xffff;
        //             printf("  copy/not: dest(16b)=0x%04x src(16b)=0x%04x\n",
        //                 dest16, src16);
        //         }
        //     } else {
        //         // Long instruction
        //         uint8_t  op     = (w0 >> 56) & 0xff;
        //         uint8_t  base   = op % 0x80;
        //         uint8_t  flags  = (w0 >> 48) & 0xff;
        //         uint8_t  bytem  = (w0 >> 32) & 0xff;
        //         uint32_t bankm  =  w0 & 0xffffffffu;
        //         printf("[CIM ISSUE][LONG ] op=0x%02x (base=0x%02x) flags=0x%02x byte_mask=0x%02x bank_mask=0x%08x col_mask=0x%016llx\n",
        //             op, base, flags, bytem, bankm, (unsigned long long)w1);

        //         if (base < 3) {
        //             // LONG_LOGIC
        //             uint8_t dest8 = (w0 >> 40) & 0xff;
        //             printf("  logic: dest(8b)=0x%02x rows(0..7) bytes=%016llx\n",
        //                 dest8, (unsigned long long)w2);
        //         } else {
        //             // LONG_COPY / LONG_NOT_COND
        //             uint16_t dest16 = (w2 >> 16) & 0xffff;
        //             uint16_t src16  =  w2        & 0xffff;
        //             printf("  copy/not: dest(16b)=0x%04x src(16b)=0x%04x\n",
        //                 dest16, src16);
        //         }
        //     }
        // }
        // volatile uint64_t *command_address = (uint64_t *)commandAddress;
        *commandAddress = command_to_send;
    }
    else
    {
        uint64_t command_to_send[3] { 0 };
        command_to_send[0] |= ((uint64_t)operation_type) << (8 * 7);
        command_to_send[0] |= ((uint64_t)operation_flag_mask) << (8 * 6);
        command_to_send[0] |= ((uint64_t)byte_mask) << (8 * 4);
        command_to_send[0] |= ((uint64_t)bank_mask);
        //
        command_to_send[1] = column_mask;
        //
        if ((operation_type % 0x80) < 3) // and or xor:
        {
            command_to_send[0] |= ((uint64_t)dest) << (8 * 5);
            for (size_t i = 0; i < row_counter; i++)
            {
                command_to_send[2] |= ((uint64_t)(row_number[i] & 0xffu))
                                      << (8 * i);
            }
        }
        else
        {
            command_to_send[2] |= ((uint64_t)dest) << (8 * 2);
            command_to_send[2] |= ((uint64_t)(row_number[0] & 0xffffu));
        }

        // volatile uint64_t *command_address = (uint64_t *)commandAddress;
        commandAddress[0] = command_to_send[0];
        commandAddress[1] = command_to_send[1];
        commandAddress[2] = command_to_send[2];
    }
}
