##
## this is based on `x86-ubuntu-run-with-kbm-no-perf.py`
## so compare with it to see the differences
##

from gem5.coherence_protocol import CoherenceProtocol
from gem5.components.boards.x86_board import X86Board
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_switchable_processor import (
    SimpleSwitchableProcessor,
)
from gem5.isas import ISA
from gem5.resources.resource import obtain_resource
from gem5.resources.resource import DiskImageResource
from gem5.simulate.exit_event import ExitEvent
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires

from gem5.components.memory.memory import ChanneledMemory


requires(
    isa_required=ISA.X86,
    kvm_required=True,
)


from gem5.components.cachehierarchies.classic.private_l1_shared_l2_cache_hierarchy import (
    PrivateL1SharedL2CacheHierarchy,
)


cache_hierarchy = PrivateL1SharedL2CacheHierarchy(
    l1d_size="16kB", l1i_size="16kB", l2_size="256kB"
)


memory = SingleChannelDDR3_1600(size="3GB")
memory._dram[0].cimObj.operations_latency = ["10ps", "10ps", "10ps"]
# memory.mem_ctrl[0].mem_sched_policy = "fcfs"

processor = SimpleSwitchableProcessor(
    starting_core_type=CPUTypes.KVM,
    switch_core_type=CPUTypes.O3,
    isa=ISA.X86,
    num_cores=2,
)

for proc in processor.start:
    proc.core.usePerf = False


board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

disk_image_path = f"/workspace/NVM_Simulation/cdnc/images/disk.raw"  # Specify the path to your disk image
# Pay attention that you are running gem5 from  devcontainer


custom_disk = DiskImageResource(disk_image_path, root_partition="1")
board.set_kernel_disk_workload(
    kernel=obtain_resource("x86-linux-kernel-5.4.0-105-generic"),
    disk_image=custom_disk,  # obtain_resource("x86-ubuntu-22.04-img"),
    readfile_contents="A message from OurConfig.py file\n",
    kernel_args=[
        ##### recommended by gem5 contributors:
        "earlyprintk=ttyS0",
        "console=ttyS0",
        "lpj=7999923",  # The lpj parameter helps the kernel accurately delay operations
        # for a specified period by calibrating the CPU's ability to perform busy-wait loops.
        ##### I added:
        "root=/dev/sda2",  # specifying second partition as root (/) directory partition
        "quiet splash memmap=16M$1536M",  # 0x60000000-0x60ffffff : Reserved
        "nokaslr",  # disabling Kernel Address Space Layout Randomization security feature
        # by disabling this you might see some Errors during the boot process
    ],
)


simulator = Simulator(
    board=board,
    on_exit_event={
        # Here we want override the default behavior for the first m5 exit
        # exit event. Instead of exiting the simulator, we just want to
        # switch the processor. The 2nd m5 exit after will revert to using
        # default behavior where the simulator run will exit.
        ExitEvent.EXIT: (func() for func in [processor.switch])
    },
)
simulator.run()
