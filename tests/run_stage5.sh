#!/bin/bash
# Stage 5: on-screen presentation in the user's nested sway (wayland-1), which only offers wl_shm.
# Mesa's Wayland WSI is put in software-present mode (sw,noshm): the GPU renders, then every frame
# is copied by the CPU into a wl_shm buffer.
#   5a  es2gears on llvmpipe (CPU only), 12 s: baseline for comparison
#   5b  vkcube on radv-xclipse, 900 frames: Vulkan presenting through WSI
#   5c  es2gears on Zink + radv-xclipse, 12 s: OpenGL ES on the GPU, presenting through kopper
# Reads the GPU reset counter before and after the GPU steps. Stops at the first GPU failure.
cd /root/src/vk-tests || exit 1
L=/root/src/vk-tests/stage5-$(date +%H%M%S).log
export WAYLAND_DISPLAY=wayland-1
unset DISPLAY
ICD=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json
{
   python3 -I gpu_health.py inicio

   echo "=== 5a: es2gears, llvmpipe (CPU) ==="
   timeout 12 stdbuf -oL es2gears_wayland 2>&1 | grep -iE 'fps|renderer|error' | head -5

   echo "=== 5b: vkcube, radv-xclipse (GPU) ==="
   t0=$(date +%s.%N)
   RADV_DEBUG=nodma VK_DRIVER_FILES=$ICD MESA_VK_WSI_DEBUG=sw,noshm RADV_XCLIPSE_LOG=1 VK_LOADER_DEBUG=error \
      timeout 60 vkcube --wsi wayland --c 900
   rc=$?
   t1=$(date +%s.%N)
   python3 -I -c "import sys; d=float(sys.argv[2])-float(sys.argv[1]); print('exit=%s  900 frames in %.1f s (~%.0f fps, startup included)' % (sys.argv[3], d, 900/d))" "$t0" "$t1" "$rc"
   python3 -I gpu_health.py tras-vkcube
   [ $rc -eq 0 ] || { echo "DETENIDO en 5b"; exit 0; }

   echo "=== 5c: es2gears, Zink on radv-xclipse (GPU) ==="
   env -u LIBGL_ALWAYS_SOFTWARE MESA_LOADER_DRIVER_OVERRIDE=zink RADV_DEBUG=nodma VK_DRIVER_FILES=$ICD \
      MESA_VK_WSI_DEBUG=sw,noshm RADV_XCLIPSE_LOG=1 VK_LOADER_DEBUG=error \
      timeout 12 stdbuf -oL es2gears_wayland 2>&1 | grep -iE 'fps|renderer|error|zink|fail' | head -8
   python3 -I gpu_health.py tras-zink
} 2>&1 | grep -v 'TITAN regmap lands' | tee "$L"
echo "log: $L"
