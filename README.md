# radv-xclipse-540

Experimental build of the community RADV (Vulkan) driver for Samsung Xclipse GPUs, extended to
the **Xclipse 540** (Exynos 1580, Galaxy Tab S10 FE) and built for **glibc Linux** (Arch Linux ARM
under proot), instead of Android.

- Base: [JimVulkan/radv-xclipse](https://github.com/JimVulkan/radv-xclipse) `313870e` plus
  [PR #6](https://github.com/JimVulkan/radv-xclipse/pull/6) (Xclipse 940), from
  `yoro1836/radv-xclipse@d3f2c124`.
- `patches/0001-xclipse-540.diff`: maps MGFX2_MID (chip_rev gen 2, mod 0x30) to a new
  `AC_XCLIPSE_540` on the 940/TITAN path (register remap, context init, no CP DMA prefetch).
  The s5e8855 kernel builds with `CONFIG_GPU_VERSION_M2_MID`, which uses the same register
  header as the 940.
- `compat/`: header-only stand-ins for `<android/log.h>` and `<sys/system_properties.h>`.
  Android properties become environment variables (`debug.radv_xclipse_ctxinit` →
  `DEBUG_RADV_XCLIPSE_CTXINIT`).

GitHub Actions builds it natively on an arm64 runner inside an Arch Linux ARM container. The
artifact is a tarball of `/opt/radv-xclipse-540`. Select it per process with
`VK_DRIVER_FILES=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json`. It is built
with the X11 and Wayland WSI platforms: X11 clients (Xwayland, Wine/Proton, Zink) need
`VK_KHR_xcb_surface` / `VK_KHR_xlib_surface`.

**Status (2026-10-11, SM-X520):** the device enumerates, a compute dispatch is correct and an
offscreen triangle renders pixel-exact on the graphics queue, with no GPU resets. See
[`tests/`](tests/). A GPU hang resets the GPU shared with Android and can crash the launcher or
reboot the device.

**Presentation (2026-10-28, SM-X520, DroidDeck 0.6.0 runtime r9):** `vkcube` runs under gamescope
at the panel's 90 Hz, the Steam client renders through Zink, and a Direct3D 9 game (Proton ARM64 +
DXVK) runs, all on this driver, with frames passed to Android as linear dma-bufs. Long sessions
are untested. Two things outside the driver were needed there:

- DroidDeck's preload hooks `drmPrimeHandleToFD` / `drmPrimeFDToHandle` and, off KGSL, looks the
  real functions up with `dlsym(RTLD_NEXT)`, which misses the libdrm the Vulkan loader opened
  `RTLD_LOCAL`. dma-buf export then fails and swapchain creation returns
  `VK_ERROR_OUT_OF_DEVICE_MEMORY`. `LD_PRELOAD=/usr/lib/libdrm.so.2` works around it.
- The compositor on the Android side must use the system Vulkan driver, not Turnip.
