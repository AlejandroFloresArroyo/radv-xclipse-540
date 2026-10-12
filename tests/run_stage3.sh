#!/bin/bash
# Stage 3 (graphics queue): 3a gfxempty, then 3b triangle. Stops at the first failure.
# Reads the GPU reset counter (VRAM_LOST_COUNTER) before and after each step.
cd /root/src/vk-tests || exit 1
L=/root/src/vk-tests/stage3-$(date +%H%M%S).log
export VK_DRIVER_FILES=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json
export RADV_XCLIPSE_LOG=1 MESA_SHADER_CACHE_DISABLE=true VK_LOADER_DEBUG=error
unset WAYLAND_DISPLAY DISPLAY
{
   python3 -I gpu_health.py inicio
   for s in gfxempty triangle; do
      echo "=== 3:$s ==="
      timeout 40 ./stage3_graphics "$s"
      rc=$?
      echo "exit=$rc"
      python3 -I gpu_health.py "tras-$s"
      [ $rc -eq 0 ] || { echo "DETENIDO en $s"; break; }
   done
} 2>&1 | grep -v 'TITAN regmap lands' | tee "$L"
echo "log: $L"
