import m5
from m5.objects import *
from gem5.runtime import get_runtime_isa


# Add the common scripts to our path
m5.util.addToPath("../")

# import the caches which we made
from caches import *

# import the SimpleOpts module
from common import SimpleOpts

# Default to running 'hello', use the compiled ISA to find the binary
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))
default_binary = os.path.join(
    thispath,
    "../../",
    "tests/test-progs/CDNCcim/aaaaaaaaaaaaa.exe",
)

# Binary to execute
SimpleOpts.add_option("binary", nargs="?", default=default_binary)

EndAddress = 0x12000018

SimpleOpts.add_option(
    "--EndAddress",
    type=str,
    help="End Address value for CIM module.",
    default="0x12000018",
)

# Finalize the arguments and grab the args so we can pass it on to our objects
args = SimpleOpts.parse_args()


EndAddress = int(args.EndAddress, 0)


# create the system we are going to simulate
system = System()

# Set the clock frequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = "timing"  # Use timing accesses
num_memctrl = 1
system.mem_ranges = [AddrRange("1GiB")]  # Create an address range

# Create Multiple CPUs
num_cpus = 1
system.cpu = [DerivO3CPU() for i in range(num_cpus)]

# X86MinorCPU() #X86O3CPU() #X86TimingSimpleCPU()


# Create an L1 instruction and data cache
for cpu in system.cpu:
    cpu.LQEntries = 32
    cpu.SQEntries = 32
    cpu.numPhysIntRegs = 128
    cpu.numPhysFloatRegs = 128
    cpu.numPhysVecRegs = 128
    cpu.numIQEntries = 64
    cpu.numROBEntries = 128

    cpu.icache = L1ICache(args)

    # cpu.icache.assoc = 1
    # cpu.icache.tag_latency = 1
    # cpu.icache.data_latency = 1
    # cpu.icache.response_latency = 1

    cpu.dcache = L1DCache(args)

    # cpu.dcache.assoc = 1
    # cpu.dcache.tag_latency = 1
    # cpu.dcache.data_latency = 1
    # cpu.dcache.response_latency = 1
    # cpu.dcache.size = "16kB"

    # Connect the instruction and data caches to the CPU
    cpu.icache.connectCPU(cpu)
    cpu.dcache.connectCPU(cpu)

# Create a memory bus, a coherent crossbar, in this case
system.l2bus = L2XBar()
system.l2bus.point_of_coherency = True

for cpu in system.cpu:
    # Hook the CPU ports up to the l2bus
    cpu.icache.connectBus(system.l2bus)
    cpu.dcache.connectBus(system.l2bus)

# Create an L2 cache and connect it to the l2bus
system.l2cache = L2Cache(args)

# system.l2cache.size = "64kB"
# system.l2cache.tag_latency = 1
# system.l2cache.data_latency = 1
# system.l2cache.response_latency = 1

system.l2cache.connectCPUSideBus(system.l2bus)

# Create a memory bus
system.membus = SystemXBar()
system.membus.frontend_latency = 10
system.membus.forward_latency = 10
system.membus.response_latency = 10
system.membus.snoop_response_latency = 10

# Connect the L2 cache to the membus
system.l2cache.connectMemSideBus(system.membus)

# create the interrupt controller for the CPU
for cpu in system.cpu:
    cpu.createInterruptController()
    # cpu.interrupts[0].pio = system.membus.mem_side_ports
    # cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    # cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# Create a memory controller

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
system.mem_ctrl.dram.banks_per_rank = 16

system.mem_ctrl.dram.range = system.mem_ranges[0]

# Scons should be build with 'CDNCcim=1' flag!!!
# otherwise comment these lines and also
# process.map(...)
try:
    system.mem_ctrl.dram.cim_handler_list = [CimHandler()]

    for cim in system.mem_ctrl.dram.cim_handler_list:
        cim.cim_operation_handler = (
            CimOperationInterface()
        )  # or CimFaultInjection()
        cim.operations_init_latency = [
            "75ps",  # these values will be multiplied by num_banks
            "75ps",
            "75ps",
            "100ps",
            "100ps",
        ]
        cim.operations_on_word_latency = [
            "5ps",  # these values will be multiplied by num_banks * num_columns
            "5ps",
            "5ps",
            "10ps",
            "10ps",
        ]
except:
    RED_TEXT = "\033[91m"
    RESET_COLOR = "\033[0m"
    print(
        RED_TEXT
        + ">>> gem5 might not have been built with the 'CDNCcim=1' flag!\n"
        + RESET_COLOR
    )

system.workload = SEWorkload.init_compatible(args.binary)

# Create a process for a simple "Hello World" application
process = Process()


# Set the command
# cmd is a list which begins with the executable (like argv)
process.cmd = [args.binary]

for cpu in system.cpu:
    # Set the cpu to use the process as its workload and create thread contexts
    cpu.workload = process
    cpu.createThreads()


# Needed for running C++ threads //I am not sure
from common.FileSystemConfig import config_filesystem

# Set up the pseudo file system for the threads function above
config_filesystem(system)


# set up the root SimObject and start the simulation
root = Root(full_system=False, system=system)
# instantiate all of the objects we've created above
m5.instantiate()

process.map(
    vaddr=Addr(0x10000000),
    paddr=Addr(0x10000000),
    size=0x2000018,
    cacheable=False,
)


print("Beginning simulation...")
exit_event = m5.simulate()
print(f"Exiting @ tick: {m5.curTick():,}\t\tbecause: {exit_event.getCause()}.")
