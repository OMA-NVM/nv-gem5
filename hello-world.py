import m5
from m5.objects import *

# --- System ---
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '3GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'atomic'                  # fine for quick testing
system.mem_ranges = [AddrRange('1GiB')]

# CPU
system.cpu = AtomicSimpleCPU()

# No caches + a simple XBar
system.membus = SystemXBar()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.system_port = system.membus.cpu_side_ports

# --- Memory backend: SimpleMemory to hit AbstractMemory::access() ---
system.mem = SimpleMemory()
system.mem.range = system.mem_ranges[0]
system.mem.port = system.membus.mem_side_ports

# Attach CIM handlers (requires your build to define CDNCcimFlag)
try:
    from m5.objects import CimHandler, CimOperationInterface
    system.mem.cim_handler_list = [CimHandler()]
    for cim in system.mem.cim_handler_list:
        # Backend ops
        cim.cim_operation_handler = CimOperationInterface()  # or CimFaultInjection()

        # Define the three regions your user code accesses:
        cim.read_write_address = Addr(0x10000000)             # data rows base
        cim.result_temporary_buffer_address = Addr(0x11000000)
        cim.command_write_address = Addr(0x12000000)          # command base

        # Keep latencies 0ps for a quick, deterministic check
        cim.operations_init_latency    = ["0ps"] * 5
        cim.operations_on_word_latency = ["0ps"] * 5
except Exception as e:
    print("WARNING: CIM not enabled or SimObjects not found:", e)

# IRQs
system.cpu.createInterruptController()

# --- Binary (the one we just built) ---
binary = "tests/test-progs/CDNCcim/cim_and/bin/arm/linux/simple_test"
system.workload = SEWorkload.init_compatible(binary)
process = Process()
process.cmd = [binary]

system.cpu.workload = process
system.cpu.createThreads()

# Map CIM MMIO regions into the process VA == PA (SE mode)
# Covers 0x1000_0000 .. 0x1200_0018 (adjust size as you need)
root = Root(full_system=False, system=system)
m5.instantiate()
process.map(
    vaddr=Addr(0x10000000),
    paddr=Addr(0x10000000),
    size=0x2000018,
    cacheable=False,
)

print("Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")