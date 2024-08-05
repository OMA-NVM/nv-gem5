#ifdef CDNCcimFSflag

#include "cim_handler.hh"

gem5::memory::CimHandler::CimHandler(const CimHandlerParams &_p)
    : SimObject(_p),
      cimAddress(_p.cim_address),
      operationsLatency(_p.operations_latency)
{
    DPRINTF(CIMDBG, "CimHandler Constructed! this_ptr: %p\n", this);
}

gem5::memory::CimHandler::~CimHandler()
{
    DPRINTF(CIMDBG, "CimHandler Destructed! this_ptr: %p\n", this);
}

void
gem5::memory::CimHandler::cimFetchCommand(
    AbstractMemory *abstract_mem, PacketPtr pkt)
{
    if (isCimAddressRange(pkt->getAddr()) && !pkt->isRead()) {
        if ((pkt->getAddr() >= 0x60000000) && (pkt->getAddr() < 0x60000010)) {
            uint32_t *command_address = reinterpret_cast<uint32_t *>(
                abstract_mem->toHostAddr(cimAddress));

            uint32_t *sr1 = reinterpret_cast<uint32_t *>(
                abstract_mem->toHostAddr(0x60000100ul));
            uint32_t *sr2 = reinterpret_cast<uint32_t *>(
                abstract_mem->toHostAddr(0x60000200ul));
            uint32_t *dest = reinterpret_cast<uint32_t *>(
                abstract_mem->toHostAddr(0x60000300ul));

            switch (command_address[0]) // *(0x60000000)
            {
                case 0u:
                    DPRINTF(
                        CIMDBG,
                        "\033[1;32m NULL(0) command written at 0x%lx "
                        "\033[0m\n",
                        pkt->getAddr());
                    dest[0] = 0x10101010u; // <= reseting dest value for test
                    break;
                case 0xAAAAAAAAu:
                    DPRINTF(
                        CIMDBG,
                        "\033[1;32m AND command written at 0x%lx \033[0m\n",
                        pkt->getAddr());
                    dest[0] = sr1[0] & sr2[0];
                    command_address[0] = 0;
                    unitReleaseTime = operationsLatency[0];
                    // ! bug: this in not a proper way to set the delay but...
                    break;
                case 0xBBBBBBBBu:
                    DPRINTF(
                        CIMDBG,
                        "\033[1;32m OR command written at 0x%lx \033[0m\n",
                        pkt->getAddr());
                    dest[0] = sr1[0] | sr2[0];
                    command_address[0] = 0;
                    unitReleaseTime = operationsLatency[1];
                    break;
                case 0xCCCCCCCCu:
                    DPRINTF(
                        CIMDBG,
                        "\033[1;32m XOR command written at 0x%lx \033[0m\n",
                        pkt->getAddr());
                    dest[0] = sr1[0] ^ sr2[0];
                    command_address[0] = 0;
                    unitReleaseTime = operationsLatency[2];
                    break;

                default:
                    DPRINTF(
                        CIMDBG,
                        "\033[1;32m dummy command with value "
                        "0x%08x \033[0m\n",
                        command_address[0]);
                    break;
            }
        }
    }
}

gem5::Tick
gem5::memory::CimHandler::getCimLatency(const Addr &addr)
{
    if ((addr >= 0x60000000) && (addr < 0x61000000)) {
        DPRINTF(
            CIMDBG,
            "\033[1;33m getCimLatency() address: 0x%lx, release time: "
            "%ld\033[0m\n",
            addr, unitReleaseTime);
        return unitReleaseTime;
    }
    return 0ul;
}

#endif // CDNCcimFSflag