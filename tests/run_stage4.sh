#!/bin/bash
# Stage 4: OpenGL ES via Zink (system Mesa) on radv-xclipse, offscreen (EGL surfaceless).
# Reads the GPU reset counter before and after.
cd /root/src/vk-tests || exit 1
L=/root/src/vk-tests/stage4-$(date +%H%M%S).log
unset WAYLAND_DISPLAY DISPLAY LIBGL_ALWAYS_SOFTWARE
export VK_DRIVER_FILES=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json
export MESA_LOADER_DRIVER_OVERRIDE=zink RADV_XCLIPSE_LOG=1 MESA_SHADER_CACHE_DISABLE=true VK_LOADER_DEBUG=error
{
   python3 -I gpu_health.py inicio
   echo "=== 4:zink ==="
   timeout 60 ./stage4_zink
   echo "exit=$?"
   python3 -I gpu_health.py despues
} 2>&1 | grep -v 'TITAN regmap lands' | tee "$L"
echo "log: $L"
