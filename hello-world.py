import m5
from m5.objects import *
from configs.common import SimpleOpts
from m5.objects import Cache

# --- System ---
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '3GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# timing
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('1GiB')]

# CPU TimingSimpleCPU
system.cpu = TimingSimpleCPU()

# Bus
system.membus = SystemXBar()
system.membus.frontend_latency = 10
system.membus.forward_latency = 10
system.membus.response_latency = 10
system.membus.snoop_response_latency = 10

class L1ICache(Cache):
    size = "32kB"
    assoc = 2
    tag_latency = 1
    data_latency = 1
    response_latency = 1
    mshrs = 4
    tgts_per_mshr = 20

class L1DCache(Cache):
    size = "32kB"
    assoc = 2
    tag_latency = 1
    data_latency = 1
    response_latency = 1
    mshrs = 8
    tgts_per_mshr = 20
    write_buffers = 8

# L1 caches
system.cpu.icache = L1ICache()
system.cpu.dcache = L1DCache()

# CPU <-> L1
system.cpu.icache_port = system.cpu.icache.cpu_side
system.cpu.dcache_port = system.cpu.dcache.cpu_side

# L1 <-> membus
system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports

system.system_port = system.membus.cpu_side_ports

system.mem_ctrl = MemCtrl()
system.mem_ctrl.mem_sched_policy = "fcfs"
system.mem_ctrl.min_writes_per_switch = 1
system.mem_ctrl.min_reads_per_switch = 1
system.mem_ctrl.static_frontend_latency = "5ns"
system.mem_ctrl.static_backend_latency = "5ns"
system.mem_ctrl.command_window = "5ns"
system.mem_ctrl.port = system.membus.mem_side_ports

system.mem_ctrl.dram = NVMInterface()
system.mem_ctrl.dram.write_buffer_size = 128
system.mem_ctrl.dram.read_buffer_size = 128
system.mem_ctrl.dram.max_pending_writes = 64
system.mem_ctrl.dram.max_pending_reads = 64
system.mem_ctrl.dram.burst_length = 8
system.mem_ctrl.dram.tCK = "1ns"
system.mem_ctrl.dram.tREAD = "680ps"
system.mem_ctrl.dram.tWRITE = "3530ps"
system.mem_ctrl.dram.tSEND = "1ns"
system.mem_ctrl.dram.tBURST = "1ns"
system.mem_ctrl.dram.tWTR = "1ns"
system.mem_ctrl.dram.tRTW = "1ns"
system.mem_ctrl.dram.tCS = "1ns"
system.mem_ctrl.dram.device_rowbuffer_size = "512B"
system.mem_ctrl.dram.device_size = "1GiB"
system.mem_ctrl.dram.device_bus_width = 64
system.mem_ctrl.dram.devices_per_rank = 1
system.mem_ctrl.dram.ranks_per_channel = 1
system.mem_ctrl.dram.banks_per_rank = 32
system.mem_ctrl.dram.range = system.mem_ranges[0]

# CIM handler 
try:
    system.mem_ctrl.dram.cim_handler_list = [CimHandler()]
    for cim in system.mem_ctrl.dram.cim_handler_list:
        cim.num_column_bits = 6
        cim.num_bank_bits   = 5
        cim.num_row_bits    = 8 

        cim.cim_operation_handler = CimOperationInterface()

        # tick  16284172194
        # ROI.  16278616755
        # cache 344133189
        # cim.operations_init_latency = [
        #     "2.821ns", "2.821ns", "2.821ns", "2.821ns", "6.56ns",
        # ]
        # cim.operations_on_word_latency = [
        #     "2.821ns", "2.821ns", "2.821ns", "2.821ns", "6.56ns",
        # ]
        # tick 16284387978
        # cache 344366955
        cim.operations_init_latency = [
            "20.821ns", "20.821ns", "20.821ns", "20.821ns", "60.56ns",
        ]
        cim.operations_on_word_latency = [
            "20.821ns", "20.821ns", "20.821ns", "20.821ns", "60.56ns",
        ]
        # cim.operations_init_latency = [
        #     "2200000.821ns", "2200000.821ns", "2200000.821ns", "2200000.821ns", "2200000.56ns",
        # ]
        # cim.operations_on_word_latency = [
        #     "2200000.821ns", "2200000.821ns", "2200000.821ns", "2200000.821ns", "2200000.56ns",
        # ]
except Exception as e:
    print("WARNING: CIM not enabled or SimObjects not found:", e) 

# IRQs
system.cpu.createInterruptController()

# --- Binary ---
binary = "./tests/test-progs/lab/bin/hello64-static"
SimpleOpts.add_option("binary", nargs="?", default=binary)

EndAddress = 0x12000018
SimpleOpts.add_option("--EndAddress", type=str, default="0x12000018")

process = Process()
process.cmd = [binary]
system.workload = SEWorkload.init_compatible(binary)
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

# data: [0x10000000, 0x12000000)  -> size = 0x2000000
process.map(vaddr=Addr(0x10000000), paddr=Addr(0x10000000),
            size=0x2000000, cacheable=True)

# cmd/mmio: [0x12000000, 0x12001000)
process.map(vaddr=Addr(0x12000000), paddr=Addr(0x12000000),
            size=0x1000, cacheable=False)

print("Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")