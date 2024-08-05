from m5.params import *
from m5.SimObject import SimObject


class CimHandler(SimObject):
    type = "CimHandler"
    cxx_header = "mem/cimTutorial/cim_handler.hh"
    cxx_class = "gem5::memory::CimHandler"

    cim_address = Param.Addr(
        Addr(0x60000000), "Starting Physical Address of our CiM module"
    )
    operations_latency = VectorParam.Latency(
        [
            "1ps",  # AND
            "1ps",  # OR
            "1ps",  # XOR
        ],
        "Arbitrary time for each operation to start performing, as an example",
    )
