#!/bin/bash
# Stage 5b diagnostics: vkcube (30 frames) with the cslog.so ioctl logger, to see which command
# submission the kernel rejects and why (chunks, ring, syncobj states). The logger only observes.
cd /root/src/vk-tests || exit 1
T=$(date +%H%M%S)
LOG=/root/src/vk-tests/cslog-vkcube-$T.txt
export WAYLAND_DISPLAY=wayland-1
unset DISPLAY
python3 -I gpu_health.py inicio
VK_DRIVER_FILES=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json \
MESA_VK_WSI_DEBUG=sw,noshm MESA_SHADER_CACHE_DISABLE=true VK_LOADER_DEBUG=error \
LD_PRELOAD=/root/src/vk-tests/cslog.so CSLOG_FILE=$LOG \
   timeout 20 vkcube --wsi wayland --c 30 2>&1 | grep -v 'TITAN regmap lands' | tail -5
echo "exit=${PIPESTATUS[0]}"
python3 -I gpu_health.py despues
echo "--- envios: $(grep -c '^CS#.* -> ok' $LOG) aceptados, $(grep -c '^CS#.* -> ' $LOG) en total"
first_bad=$(grep -m1 -E '^CS#[0-9]+ -> [^o]' $LOG | sed -E 's/^CS#([0-9]+).*/\1/')
if [ -n "$first_bad" ]; then
   echo "--- primer envio rechazado: CS#$first_bad (y el anterior, para comparar)"
   awk -v b="$first_bad" '/^CS#[0-9]+ fd=/{n=substr($1,4)+0} n==b-1||n==b' $LOG
   echo "--- ioctls fallidas antes del rechazo (sin las INFO esperadas):"
   awk -v b="$first_bad" '/^CS#[0-9]+ fd=/{n=substr($1,4)+0} n<b && /^ioctl/ && !/nr=0x45/' $LOG | sort | uniq -c | head -20
else
   echo "--- ningun envio rechazado"
fi
echo "log completo: $LOG"
