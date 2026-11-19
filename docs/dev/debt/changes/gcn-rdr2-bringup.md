# libSceAgcDriver

## add Functional

- [Context registers through SET_CONFIG_REG on a PS4](../../../core/libs/prx/libSceAgcDriver/Execution/src/Pm4.cpp) (libSceAgcDriver, PS4) - with GCN guest shaders, a SET_CONFIG_REG whose offset is 0x8000-0x83ff writes context registers 0-0x3ff. The config bank starts at 0x2000, so Red Dead Redemption 2's offset 0x8102 is VGT_INDX_OFFSET (0xa102). Other config offsets are still refused. Not measured on a console.
- [GCN vertex instance step rates](../../../core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCapture.cpp) (libSceAgcDriver, PS4) - a legacy GCN vertex program's v1 and v2 hold the instance index divided by VGT_INSTANCE_STEP_RATE_0 and _1 (context 0x2a8 and 0x2a9) when SPI_SHADER_PGM_RSRC1_VS.VGPR_COMP_CNT requests them. The rates are part of the shader cache key. A rate of 0, the CLEAR_STATE value, divides by 1, and the console result for it is not known. The rates are not in serialized recompile requests, so a replayed request uses 1.

# libSceGnmDriver

## add Functional

- [sceGnmMapComputeQueueWithPriority](../../../core/libs/prx/libSceGnmDriver/Export.cpp) (libSceGnmDriver, PS4) - maps the queue as sceGnmMapComputeQueue does and ignores the priority, because the host has no compute queue priorities.
