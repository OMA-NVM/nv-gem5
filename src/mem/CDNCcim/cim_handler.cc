#ifdef CDNCcimFlag
/**
 * CDNCcim:
 *
 * @file
 * CimHandler definition
 *      base class for CIM operation handler
 */


#include "cim_handler.hh"

namespace gem5
{
namespace memory
{
CimHandler::CimHandler(const CimHandlerParams &params)
    : SimObject(params),
      readWriteAddress(params.read_write_address),
      resultTemporaryBufferAddress(params.result_temporary_buffer_address),
      commandWriteAddress(params.command_write_address),
      numColumnBits(params.num_column_bits),
      numBankBits(params.num_bank_bits),
      numRowBits(params.num_row_bits),
      operationsInitLatency(params.operations_init_latency),
      operationsOnWordLatency(params.operations_on_word_latency),
      cimOperationHandler(params.cim_operation_handler)
{
    DPRINTF(CIMDBG, "CimHandler Constructed! this_ptr: %p\n", this);
    assert(numOperationTypes == operationsInitLatency.size());
    assert(numOperationTypes == operationsOnWordLatency.size());
    for (auto t : operationsOnWordLatency) {
        DPRINTF(CIMDBG, "operationsOnWordLatency : %ld\n", t);
    }

    // change this if you need intraBank parallelism:
    // also the other places related to 'unitReleaseTime'
    size_t parallel_units { 1u << numBankBits };
    unitReleaseTime = new Tick[parallel_units]();
    for (size_t i = 0; i < parallel_units; i++) {
        unitReleaseTime[i] = curTick();
    }
}

CimHandler::~CimHandler()
{
    if (unitReleaseTime)
        delete[] unitReleaseTime;
    unitReleaseTime = nullptr;
}

void
CimHandler::cimFetchCommand(
    AbstractMemory *abstract_mem, PacketPtr pkt, uint8_t *host_addr)
{
    DPRINTF(CIMDBG, "call cimFetchCommand\n");
    DPRINTF(
        CIMDBG, "[%s:%s:%s] from address: 0x%lx : command: 0x%016lx \n",
        __FILE__, __func__, __LINE__, pkt->getAddr(), *(uint64_t *)host_addr);
    uint64_t *command_address = reinterpret_cast<uint64_t *>(
        abstract_mem->toHostAddr(commandWriteAddress));

    if (command_address[0] == 0ul)
        return;

    CommandDecode command;

    // check to see if it is a short command:
    if (command_address[0] & (1ul << 63)) // Short instruction (64 bit)
    {
        command.operation_type = (command_address[0] >> (8 * 7)) & 0xffu;
        command.operation_flag_mask = (command_address[0] >> (8 * 6)) & 0xffu;
        command.byte_mask = (command_address[0] >> (8 * 4)) & 0xffu;

        switch (static_cast<OperationType>(command.operation_type)) {
            case OperationType::short_AND... OperationType::short_XOR:
                command.dest = (command_address[0] >> (8 * 5)) & 0xffu;
                for (int8_t i = 0; i < 4; i++) {
                    if (command.operation_flag_mask & (1u << i))
                        command.row_number[i]
                            = (command_address[0] >> (8 * i)) & 0xffu;
                }
                break;

            case OperationType::short_COPY... OperationType::short_NOT_COND:
                command.dest = (command_address[0] >> (16)) & 0xffffu;
                command.row_number[0] = command_address[0] & 0xffffu;
                break;
            default:

                panic(
                    "\n>>> Unrecognized Command issued!! : command: %x \n",
                    command.operation_type);
                break;
        }
    } else // Long instruction (3 * 64 bit)
    {
        if ((command_address[1] == 0ul) || (command_address[2] == 0ul))
            return;
        command.operation_type = (command_address[0] >> (8 * 7)) & 0xffu;
        command.operation_flag_mask = (command_address[0] >> (8 * 6)) & 0xffu;
        command.byte_mask = (command_address[0] >> (8 * 4)) & 0xffu;

        command.bank_mask = command_address[0] & 0xffffffffu;
        command.column_mask = command_address[1];

        switch (static_cast<OperationType>(command.operation_type)) {
            case OperationType::AND... OperationType::XOR:
                command.dest = (command_address[0] >> (8 * 5)) & 0xffu;
                for (int8_t i = 0; i < 8; i++) {
                    if (command.operation_flag_mask & (1u << i))
                        command.row_number[i]
                            = (command_address[2] >> (8 * i)) & 0xffu;
                }
                break;

            case OperationType::COPY... OperationType::NOT_COND:
                command.dest = (command_address[2] >> (16)) & 0xffffu;
                command.row_number[0] = command_address[2] & 0xffffu;
                break;
            default:
                panic(
                    "\n>>> Unrecognized Command issued!! : command: %x \n",
                    command.operation_type);
                break;
        }
    }
    command_address[0] = 0ul;
    command_address[1] = 0ul;
    command_address[2] = 0ul;
    cimExecuteCommand(abstract_mem, command);
}
void
CimHandler::cimExecuteCommand(
    AbstractMemory *abstract_mem, CommandDecode &command)
{
    DPRINTF(
        CIMDBG, "[%s:%s:%s] cimExecuteCommand\n", __FILE__, __func__,
        __LINE__);
    command.print();

    for (size_t bank = 0; bank < (1ul << numBankBits); bank++) {
        if (command.bank_mask & (1ul << bank)) {
            cimUpdateLatencyTable(true, command.operation_type, bank);
            for (size_t column = 0;
                 column < (1ul << (numColumnBits - byteBits)); column++) {
                if (command.column_mask & (1ul << column)) {
                    cimUpdateLatencyTable(false, command.operation_type, bank);
                    switch (
                        static_cast<OperationType>(command.operation_type)) {
                        case OperationType::AND:
                        case OperationType::short_AND: {
                            uint8_t *dest = addressTranslator(
                                abstract_mem, resultTemporaryBufferAddress,
                                command.dest, bank, column);
                            std::vector<uint8_t *> rows;
                            for (auto &row : command.row_number) {
                                if (row < (1ul << numRowBits))
                                    rows.push_back(addressTranslator(
                                        abstract_mem, readWriteAddress, row,
                                        bank, column));
                            }
                            assert(rows.size() > 1);
                            cimOperationHandler->AND(
                                rows, dest, command.byte_mask);
                            break;
                        }
                        case OperationType::OR:
                        case OperationType::short_OR: {
                            uint8_t *dest = addressTranslator(
                                abstract_mem, resultTemporaryBufferAddress,
                                command.dest, bank, column);
                            std::vector<uint8_t *> rows;
                            for (auto &row : command.row_number) {
                                if (row < (1ul << numRowBits))
                                    rows.push_back(addressTranslator(
                                        abstract_mem, readWriteAddress, row,
                                        bank, column));
                            }
                            assert(rows.size() > 1);
                            cimOperationHandler->OR(
                                rows, dest, command.byte_mask);
                            break;
                        }
                        case OperationType::XOR:
                        case OperationType::short_XOR: {
                            uint8_t *dest = addressTranslator(
                                abstract_mem, resultTemporaryBufferAddress,
                                command.dest, bank, column);
                            std::vector<uint8_t *> rows;
                            for (auto &row : command.row_number) {
                                if (row < (1ul << numRowBits))
                                    rows.push_back(addressTranslator(
                                        abstract_mem, readWriteAddress, row,
                                        bank, column));
                            }
                            assert(rows.size() > 1);
                            cimOperationHandler->XOR(
                                rows, dest, command.byte_mask);
                            break;
                        }
                        case OperationType::COPY:
                        case OperationType::short_COPY: {
                            Addr base_address
                                = (command.row_number[0] & 0xff00)
                                      ? resultTemporaryBufferAddress
                                      : readWriteAddress;
                            uint8_t *src = addressTranslator(
                                abstract_mem, base_address,
                                command.row_number[0] & 0xff, bank, column);
                            base_address = (command.dest & 0xff00)
                                               ? resultTemporaryBufferAddress
                                               : readWriteAddress;
                            uint8_t *dest = addressTranslator(
                                abstract_mem, base_address,
                                command.dest & 0xff, bank, column);

                            cimOperationHandler->COPY(
                                src, dest, command.operation_flag_mask,
                                command.byte_mask);
                            break;
                        }
                        case OperationType::NOT_COND:
                        case OperationType::short_NOT_COND: {
                            Addr base_address
                                = (command.row_number[0] & 0xff00u)
                                      ? resultTemporaryBufferAddress
                                      : readWriteAddress;
                            uint8_t *src = addressTranslator(
                                abstract_mem, base_address,
                                command.row_number[0] & 0xffu, bank, column);
                            base_address = (command.dest & 0xff00u)
                                               ? resultTemporaryBufferAddress
                                               : readWriteAddress;
                            uint8_t *dest = addressTranslator(
                                abstract_mem, base_address,
                                command.dest & 0xffu, bank, column);

                            bool always_NOT
                                = command.operation_flag_mask & 0x02u;
                            bool NOT_if_zero
                                = command.operation_flag_mask & 0x01u;
                            cimOperationHandler->NOT_COND(
                                src, dest, always_NOT, NOT_if_zero,
                                command.byte_mask);
                            break;
                        }
                        default:
                            panic("\n>>>Unpredicted Behavior!!!\n");
                            break;
                    }
                }
            }
        }
    }
}

void
CimHandler::cimUpdateLatencyTable(bool init, uint8_t operation, size_t bank)
{
    if (init) {
        unitReleaseTime[bank]
            = (static_cast<int64_t>(unitReleaseTime[bank])
                   - static_cast<int64_t>(curTick())
               > 0)
                  ? unitReleaseTime[bank]
                        + operationsInitLatency[operation % 0x80]
                  : curTick() + operationsInitLatency[operation % 0x80];
    } else {
        unitReleaseTime[bank]
            = (static_cast<int64_t>(unitReleaseTime[bank])
                   - static_cast<int64_t>(curTick())
               > 0)
                  ? unitReleaseTime[bank]
                        + operationsOnWordLatency[operation % 0x80]
                  : curTick() + operationsOnWordLatency[operation % 0x80];
    }
    // DPRINTF(
    //     CIMDBG, "[%s:%s:%s] init: %d\t unitReleaseTime[%d]: %d\n", __FILE__,
    //     __func__, __LINE__, init, bank, unitReleaseTime[bank]);
}

uint8_t *
CimHandler::addressTranslator(
    AbstractMemory *abstract_mem, Addr startAddress, uint16_t row,
    uint8_t bank, uint8_t column)
{
    uint64_t Address { startAddress };
    Address += (row & 0xffu) << (numBankBits + numColumnBits);
    Address += bank << (numColumnBits);
    Address += column << (byteBits);
    // Address += (row & 0xff00u) ? 0x1000000 : 0;
    panic_if(
        Address >= commandWriteAddress,
        ">>> Corruption in CimHandler::addressTranslator!!! \n");
    return abstract_mem->toHostAddr(Address);
    // return (uint8_t *)(Address);
}

Tick
CimHandler::getCimLatency(const Addr &addr)
{
    uint8_t bank = (addr >> (numColumnBits)) % (1u << numBankBits);
    int64_t left_time
        = (static_cast<int64_t>(unitReleaseTime[bank])
           - static_cast<int64_t>(curTick()));
    DPRINTF(
        CIMDBG, "[%s:%s:%s] left_time: %d, for address: 0x%lx\n", __FILE__,
        __func__, __LINE__, left_time, addr);

    if (left_time > 0)
        return left_time;
    return 0;
}

void
CimHandler::CommandDecode::print()
{
    DPRINTFR(CIMDBG, "-------\n** Printing command: \n");
    DPRINTFR(
        CIMDBG, "type: %02x, flag: %02x, byte_mask: %02x\n", operation_type,
        operation_flag_mask, byte_mask);
    DPRINTFR(
        CIMDBG, "bank_mask: %08x , column_mask: %016lx\n", bank_mask,
        column_mask);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[0]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[1]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[2]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[3]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[4]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[5]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[6]);
    DPRINTFR(CIMDBG, "row: %04x \n", row_number[7]);
    DPRINTFR(CIMDBG, "dest: %04x \n------\n", dest);
}

} // namespace memory
} // namespace gem5
#endif