# shader

## change [GCN image refusals]

- [GCN image refusals](../../../core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp) (shader, PS4) - these throw: a 3D T# with DA (the ISA does not say whether an index follows z), a depth comparison through a 1D or 1D-array T# (the address would be read as 2D instead of the centre of row 0), and a gather or depth comparison through a 1D or 3D T#. FMASK expansion, packed formats and element-addressed images keep the inferred dimension, so such a T# of another dimension is refused. A 2D sample through a 2D MSAA T# (table 8.9 has no such row) reads fragment 0, as the RDNA path does; this is not measured on a PS4.
