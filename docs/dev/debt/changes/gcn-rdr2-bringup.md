# libSceAgcDriver

## add Functional

- [Context registers through SET_CONFIG_REG on a PS4](../../../core/libs/prx/libSceAgcDriver/Execution/src/Pm4.cpp) (libSceAgcDriver, PS4) - with GCN guest shaders, a SET_CONFIG_REG whose offset is 0x8000-0x83ff writes context registers 0-0x3ff. The config bank starts at 0x2000, so Red Dead Redemption 2's offset 0x8102 is VGT_INDX_OFFSET (0xa102). Other config offsets are still refused. Not measured on a console.
- [1_5_5_5 targets with the alternate component order](../../../core/libs/prx/libSceAgcDriver/Graphics/src/State.cpp) (libSceAgcDriver) - COLOR_1_5_5_5 UNORM with COMP_SWAP ALT renders through A1R5G5B5 without exchanging X and Z, the layout of B5G5R5A1 in Mesa's `si_translate_colorswap`. Not measured on a console.
- [GCN vertex instance step rates](../../../core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCapture.cpp) (libSceAgcDriver, PS4) - a legacy GCN vertex program's v1 and v2 hold the instance index divided by VGT_INSTANCE_STEP_RATE_0 and _1 (context 0x2a8 and 0x2a9) when SPI_SHADER_PGM_RSRC1_VS.VGPR_COMP_CNT requests them. The rates are part of the shader cache key. A rate of 0, the CLEAR_STATE value, divides by 1, and the console result for it is not known. The rates are not in serialized recompile requests, so a replayed request uses 1.

# libSceGnmDriver

## add Functional

- [sceGnmMapComputeQueueWithPriority](../../../core/libs/prx/libSceGnmDriver/Export.cpp) (libSceGnmDriver, PS4) - maps the queue as sceGnmMapComputeQueue does and ignores the priority, because the host has no compute queue priorities.
- [PS4 Gnm shader update and compute writers](../../../core/libs/prx/libSceGnmDriver/Export.cpp) (libSceGnmDriver, PS4) - sceGnmUpdateVsShader, UpdateGsShader, UpdateHsShader and UpdatePsShader350 write the same register packets as the Set forms. They do not use NOP-wrapped context writes, because the AGC driver applies only real register packets. UpdateHsShader pads with a NOP up to the size the caller reserved. sceGnmSetPsShader350, SetCsShaderWithModifier, DispatchDirect, DispatchIndirect, DispatchIndirectOnMec, ComputeWaitOnAddress and ComputeWaitSemaphore follow the packets of libSceGnmDriverForNeoMode. sceGnmGetPhysicalCounterFromVirtualized fails, and sceGnmLogicalTcaUnitToPhysical is not implemented. Not measured on a console.

# libSceVideoRecording

## add Functional

- [PS4 recording imports](../../../core/libs/prx/libSceVideoRecording/CMakeLists.txt) (libSceVideoRecording, PS4) - libSceVideoRecording is built from the libSceVideoRecordingP sources, because PS4 titles import the same API under that library name.
- [PS4 draw command buffer chains](../../../core/libs/prx/libSceGnmDriver/Export.cpp) (libSceGnmDriver, PS4) - an INDIRECT_BUFFER in a draw command buffer is followed at submission. A call runs its buffer and returns, at most two levels deep. A chain continues in its buffer and skips the rest of the current one. A released REWIND is dropped. An unreleased REWIND ends the submission: a detached thread waits for its valid bit every 0.1 ms, then lowers and submits the words after it. Later submissions and sceGnmSubmitDone wait behind it in order. An unreleased REWIND inside a called buffer, and predicated INDIRECT_BUFFER packets, are not implemented. VideoOut flips that the title submits directly do not wait behind a stalled REWIND.
