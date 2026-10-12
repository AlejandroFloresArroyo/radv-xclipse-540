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
`VK_DRIVER_FILES=/opt/radv-xclipse-540/share/vulkan/icd.d/radeon_icd.aarch64.json`.

**Untested on hardware.** A GPU hang resets the GPU shared with Android and can crash the
launcher or reboot the device.
