# Hardware bring-up tests

Staged tests for the Xclipse 540, each one step riskier than the last. Before and after every
step, `gpu_health.py` reads the amdgpu `VRAM_LOST_COUNTER`, which goes up on a GPU reset.

| Stage | Program | What reaches the GPU |
|---|---|---|
| 1 | `stage1_enum` | Nothing: instance, physical-device enumeration and properties only |
| 2a | `stage2_compute device` | Nothing: `vkCreateDevice` / `vkDestroyDevice` |
| 2b | `stage2_compute empty` | Nothing: an empty submit only moves already-signaled syncobjs |
| 2c | `stage2_compute compute` | One 64-thread dispatch on a compute-only queue |
| 3a | `stage3_graphics gfxempty` | The graphics preamble (Xclipse context init + TITAN remap) |
| 3b | `stage3_graphics triangle` | Clear, one triangle, copy to buffer (256x256 RGBA8, no depth) |

Build the tests (shader headers come from glslang):

```sh
glslangValidator -V --target-env vulkan1.3 --vn stage2_fill_spv -o stage2_fill.h stage2_fill.comp
glslangValidator -V --target-env vulkan1.3 --vn stage3_vert_spv -o stage3_vert.h stage3_tri.vert
glslangValidator -V --target-env vulkan1.3 --vn stage3_frag_spv -o stage3_frag.h stage3_tri.frag
for t in stage1_enum stage2_compute stage3_graphics; do
  gcc -O1 -Wall -I<path-to-vulkan-headers> -o $t $t.c -l:libvulkan.so.1
done
```

Run them with
`VK_DRIVER_FILES=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json`.

## Results on a Galaxy Tab S10 FE (SM-X520, Exynos 1580), 2026-10-11

- Stage 1: `AMD Radeon Graphics (RADV TITAN)`, Vulkan 1.4.358, `model=540`, TITAN remap level
  10, one 7184 MiB device-local heap.
- Stage 2: all steps pass. The dispatch reads back 64/64 correct values.
- Stage 3: all steps pass. The triangle is exact: 18432 orange pixels (the expected 28.125 %),
  47104 blue, 0 wrong (`stage3-triangle-xclipse540.png`).
- `VRAM_LOST_COUNTER` stayed at 0 throughout.
