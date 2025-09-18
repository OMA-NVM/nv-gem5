from m5.params import *
from m5.SimObject import SimObject


class CimHandler(SimObject):
    type = "CimHandler"
    cxx_header = "mem/CDNCcim/cim_handler.hh"
    cxx_class = "gem5::memory::CimHandler"

    # ============= Adress format:
    read_write_address = Param.Addr(
        Addr(0x10000000), "Physical address of the CIM operation region"
    )
    result_temporary_buffer_address = Param.Addr(
        Addr(0x11000000),
        """Physical address of The temporary buffer region.
        CPU must not read/write to/from this region!!!""",
    )
    command_write_address = Param.Addr(
        Addr(0x12000000), "Physical address of The command opcode"
    )
    # ============== Address Decoder:
    # <-0x10?????? >
    # <-res-><(8)bit for row><(3~5) bits for banks><(5~9) bits for column>
    # <--------------------------------- 4 * 6 = 24  bits --------------->

    num_column_bits = Param.UInt8(9, "Size of each row in a bank = 512 Bytes")
    num_bank_bits = Param.UInt8(4, "Number of banks = 16")
    num_row_bits = Param.UInt8(8, "Number of CIM capable rows pre bank = 256")

    # ============== Latency:
    operations_init_latency = VectorParam.Latency(
        [
            "10ns",  # AND
            "10ns",  # OR
            "10ns",  # XOR
            "10ns",  # NOT+CONDITION: (Always NOT, NOT if (Zero) or (NonZero))
            "10ns",  # COPY+LeftRotate:
            # (Internal copy inside the CIM region.
            # Also used for write back the result from output buffer to CIM)
        ],
        "Initial time for each operation to start performing",
    )
    operations_on_word_latency = VectorParam.Latency(
        [
            "10ns",  # AND
            "10ns",  # OR
            "10ns",  # XOR
            "10ns",  # NOT+CONDITION: (Always NOT, NOT if (Zero) or (NonZero))
            "10ns",  # COPY+LeftRotate:
            # (Internal copy inside the CIM region.
            # Also used for write back the result from output buffer to CIM)
        ],
        """Time used for each word length of data(8 Byte here!)
        to be read and perform the operation on them!""",
    )
    # ============== Operation Interface:
    cim_operation_handler = Param.CimOperationInterface(
        "Operations Are defined in this interface object",
    )
