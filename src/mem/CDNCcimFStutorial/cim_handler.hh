#ifdef CDNCcimFSflag

#ifndef __CIM_HANDLER_HH__
#define __CIM_HANDLER_HH__

#include "mem/abstract_mem.hh"
#include "mem/mem_ctrl.hh"
#include "sim/sim_object.hh"

// below header files will be automatically generated by gem5
#include "debug/CIMDBG.hh"
#include "params/CimHandler.hh"

namespace gem5
{
namespace memory
{

class CimHandler : public SimObject
{
  private:
    /// @brief starting physical address of our cim Module
    Addr cimAddress;

    /// @brief for each operation, we might have different delay (1Tick = 1ps)
    std::vector<Tick> operationsLatency;

    /// @brief just a basic busy-time holder
    Tick unitReleaseTime;

  public:
    CimHandler(const CimHandlerParams &_p);
    ~CimHandler();

    /// @brief Checks to see if `addr` is in our cim Region or not
    /// @param addr
    /// @return
    inline bool isCimAddressRange(const Addr &addr) const
    {
        return (addr >= cimAddress) && (addr < (cimAddress + 0x1000000));
    }

    /// @brief when something is written to `cimAddress` this would be called
    /// @param abstract_mem to get the host array address
    /// @param pkt to see what is accessing this region
    void cimFetchCommand(AbstractMemory *abstract_mem, PacketPtr pkt);

    /// @brief when something wants to read or write to CimAddressRange this
    /// would be called
    /// @param addr
    /// @return return time to get packet
    Tick getCimLatency(const Addr &addr);
};
} // namespace memory
} // namespace gem5

#endif //__CIM_HANDLER_HH__
#endif // CDNCcimFSflag