import m5
from m5.objects import *
from configs.common import SimpleOpts

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

system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
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
        cim.num_bank_bits   = 0
        cim.num_row_bits    = 10

        cim.cim_operation_handler = CimOperationInterface()

        cim.operations_init_latency = [
            "2.821ns", "2.821ns", "2.821ns", "2.821ns", "6.56ns",
        ]
        cim.operations_on_word_latency = [
            "2.821ns", "2.821ns", "2.821ns", "2.821ns", "6.56ns",
        ]
except Exception as e:
    print("WARNING: CIM not enabled or SimObjects not found:", e) 

# IRQs
system.cpu.createInterruptController()

# --- Binary ---
binary = "./tests/test-progs/lab/bin/hello64-static"
SimpleOpts.add_option("binary", nargs="?", default=binary)

EndAddress = 0x12000018
SimpleOpts.add_option("--EndAddress", type=str, default="0x12000018")

system.workload = SEWorkload.init_compatible(binary)
process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

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