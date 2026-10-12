#!/usr/bin/env python3
"""Read-only GPU health check: prints the amdgpu VRAM_LOST counter (bumps on every GPU reset
that loses memory). Only issues an AMDGPU_INFO ioctl; never submits work."""
import ctypes
import fcntl
import os
import sys

DRM_IOCTL_AMDGPU_INFO = 0x40206445  # DRM_IOW(0x40 + 0x05, struct drm_amdgpu_info), 32 bytes
AMDGPU_INFO_VRAM_LOST_COUNTER = 0x1F


class DrmAmdgpuInfo(ctypes.Structure):
    _fields_ = [
        ("return_pointer", ctypes.c_uint64),
        ("return_size", ctypes.c_uint32),
        ("query", ctypes.c_uint32),
        ("pad", ctypes.c_uint8 * 16),
    ]


def vram_lost(node="/dev/dri/renderD128"):
    fd = os.open(node, os.O_RDWR | os.O_CLOEXEC)
    try:
        out = ctypes.c_uint32(0)
        req = DrmAmdgpuInfo(ctypes.addressof(out), 4, AMDGPU_INFO_VRAM_LOST_COUNTER)
        fcntl.ioctl(fd, DRM_IOCTL_AMDGPU_INFO, req)
        return out.value
    finally:
        os.close(fd)


if __name__ == "__main__":
    print(f"{sys.argv[1] if len(sys.argv) > 1 else 'check'}: VRAM_LOST_COUNTER={vram_lost()}")
