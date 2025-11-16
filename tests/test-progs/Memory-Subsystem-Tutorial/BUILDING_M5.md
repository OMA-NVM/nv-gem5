# Building the m5 Utility for NVMain

Before compiling and running the tutorial workloads with `gem5-opt`, make sure
the `m5` utility is also built with the NVMain extras. From the gem5 repo root,
run:

```bash
python3 `which scons` -j8 EXTRAS=../nvmain -C util/m5 build/x86/out/m5```



