/*
 * cslog.so: LD_PRELOAD logger for amdgpu command submissions (diagnostics only; changes nothing).
 * Wraps ioctl(). For every DRM_IOCTL_AMDGPU_CS it prints the chunks (IBs with ring/flags/size,
 * syncobj waits/signals, dependencies, BO list size) and, for each syncobj the CS waits on, what
 * the kernel reports for it right before submission (signaled / pending / no fence). It also
 * prints every DRM ioctl that fails. Output: $CSLOG_FILE (default stderr).
 *
 * gcc -O1 -Wall -fPIC -shared -o cslog.so cslog.c -ldl -lpthread
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define DRM_TYPE 'd'
#define NR_AMDGPU_CS 0x44
#define NR_SYNCOBJ_WAIT 0xC3
#define NR_SYNCOBJ_TIMELINE_WAIT 0xCA

struct cs_in {
   uint32_t ctx_id, bo_list_handle, num_chunks, flags;
   uint64_t chunks;
};
struct cs_chunk {
   uint32_t chunk_id, length_dw;
   uint64_t chunk_data;
};
struct chunk_ib {
   uint32_t pad, flags;
   uint64_t va_start;
   uint32_t ib_bytes, ip_type, ip_instance, ring;
};
struct chunk_fence {
   uint32_t handle, offset;
};
struct chunk_dep {
   uint32_t ip_type, ip_instance, ring, ctx_id;
   uint64_t handle;
};
struct chunk_sem {
   uint32_t handle;
};
struct chunk_syncobj {
   uint32_t handle, flags;
   uint64_t point;
};
struct bo_list_in {
   uint32_t operation, list_handle, bo_number, bo_info_size;
   uint64_t bo_info_ptr;
};
struct syncobj_wait {
   uint64_t handles;
   int64_t timeout_nsec;
   uint32_t count_handles, flags, first_signaled, pad;
};
struct syncobj_timeline_wait {
   uint64_t handles, points;
   int64_t timeout_nsec;
   uint32_t count_handles, flags, first_signaled, pad;
};

static int (*real_ioctl)(int, unsigned long, ...);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *out;
static unsigned long seq;

static void
init(void)
{
   if (!real_ioctl)
      real_ioctl = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
   if (!out) {
      const char *p = getenv("CSLOG_FILE");
      out = p ? fopen(p, "a") : NULL;
      if (!out)
         out = stderr;
      setvbuf(out, NULL, _IOLBF, 0);
   }
}

/* What the kernel says about a wait syncobj right now: 0 signaled, -ETIME pending, -EINVAL no fence. */
static const char *
probe_binary(int fd, uint32_t handle)
{
   struct syncobj_wait w = {.handles = (uintptr_t)&handle, .timeout_nsec = 0, .count_handles = 1};
   int r = real_ioctl(fd, _IOWR(DRM_TYPE, NR_SYNCOBJ_WAIT, struct syncobj_wait), &w);
   if (r == 0)
      return "signaled";
   return errno == ETIME ? "pending" : errno == EINVAL ? "NO-FENCE" : strerror(errno);
}

static const char *
probe_timeline(int fd, uint32_t handle, uint64_t point)
{
   struct syncobj_timeline_wait w = {.handles = (uintptr_t)&handle,
                                     .points = (uintptr_t)&point,
                                     .timeout_nsec = 0,
                                     .count_handles = 1};
   int r = real_ioctl(fd, _IOWR(DRM_TYPE, NR_SYNCOBJ_TIMELINE_WAIT, struct syncobj_timeline_wait), &w);
   if (r == 0)
      return "signaled";
   return errno == ETIME ? "pending" : errno == EINVAL ? "NO-FENCE" : strerror(errno);
}

static void
log_cs(int fd, const struct cs_in *cs, unsigned long n)
{
   fprintf(out, "CS#%lu fd=%d ctx=%u bo_list=%u flags=0x%x chunks=%u\n", n, fd, cs->ctx_id, cs->bo_list_handle,
           cs->flags, cs->num_chunks);
   const uint64_t *ptrs = (const uint64_t *)(uintptr_t)cs->chunks;
   for (uint32_t i = 0; i < cs->num_chunks && ptrs; i++) {
      const struct cs_chunk *c = (const struct cs_chunk *)(uintptr_t)ptrs[i];
      const void *d = (const void *)(uintptr_t)c->chunk_data;
      switch (c->chunk_id) {
      case 0x01: {
         const struct chunk_ib *ib = d;
         fprintf(out, "  IB      ip=%u inst=%u ring=%u flags=0x%x bytes=%u va=0x%llx\n", ib->ip_type,
                 ib->ip_instance, ib->ring, ib->flags, ib->ib_bytes, (unsigned long long)ib->va_start);
         break;
      }
      case 0x02: {
         const struct chunk_fence *f = d;
         fprintf(out, "  FENCE   handle=%u offset=%u\n", f->handle, f->offset);
         break;
      }
      case 0x03:
      case 0x07: {
         const struct chunk_dep *dep = d;
         unsigned cnt = c->length_dw * 4 / sizeof(*dep);
         for (unsigned k = 0; k < cnt; k++)
            fprintf(out, "  %s ip=%u ring=%u ctx=%u seq=%llu\n", c->chunk_id == 3 ? "DEP    " : "SCHEDEP",
                    dep[k].ip_type, dep[k].ring, dep[k].ctx_id, (unsigned long long)dep[k].handle);
         break;
      }
      case 0x04:
      case 0x05: {
         const struct chunk_sem *s = d;
         unsigned cnt = c->length_dw * 4 / sizeof(*s);
         for (unsigned k = 0; k < cnt; k++)
            fprintf(out, "  %s syncobj=%u%s%s\n", c->chunk_id == 4 ? "WAIT   " : "SIGNAL ", s[k].handle,
                    c->chunk_id == 4 ? " state=" : "", c->chunk_id == 4 ? probe_binary(fd, s[k].handle) : "");
         break;
      }
      case 0x06: {
         const struct bo_list_in *b = d;
         fprintf(out, "  BOLIST  op=%u count=%u entry_size=%u\n", b->operation, b->bo_number, b->bo_info_size);
         break;
      }
      case 0x08:
      case 0x09: {
         const struct chunk_syncobj *s = d;
         unsigned cnt = c->length_dw * 4 / sizeof(*s);
         for (unsigned k = 0; k < cnt; k++)
            fprintf(out, "  %s syncobj=%u point=%llu flags=0x%x%s%s\n", c->chunk_id == 8 ? "TLWAIT " : "TLSIGNL",
                    s[k].handle, (unsigned long long)s[k].point, s[k].flags, c->chunk_id == 8 ? " state=" : "",
                    c->chunk_id == 8 ? probe_timeline(fd, s[k].handle, s[k].point) : "");
         break;
      }
      default:
         fprintf(out, "  CHUNK   id=0x%x length_dw=%u\n", c->chunk_id, c->length_dw);
      }
   }
}

int
ioctl(int fd, unsigned long req, ...)
{
   va_list ap;
   va_start(ap, req);
   void *arg = va_arg(ap, void *);
   va_end(ap);
   init();

   if (_IOC_TYPE(req) != DRM_TYPE)
      return real_ioctl(fd, req, arg);

   const unsigned nr = _IOC_NR(req);
   unsigned long n = 0;
   if (nr == NR_AMDGPU_CS && arg) {
      pthread_mutex_lock(&lock);
      n = ++seq;
      log_cs(fd, arg, n);
      pthread_mutex_unlock(&lock);
   }
   int r = real_ioctl(fd, req, arg);
   int e = errno;
   if (nr == NR_AMDGPU_CS) {
      pthread_mutex_lock(&lock);
      fprintf(out, "CS#%lu -> %s\n", n, r == 0 ? "ok" : strerror(e));
      pthread_mutex_unlock(&lock);
   } else if (r != 0 && !(nr == NR_SYNCOBJ_WAIT && e == ETIME) && !(nr == NR_SYNCOBJ_TIMELINE_WAIT && e == ETIME)) {
      pthread_mutex_lock(&lock);
      fprintf(out, "ioctl fd=%d nr=0x%02x size=%u -> %s\n", fd, nr, (unsigned)_IOC_SIZE(req), strerror(e));
      pthread_mutex_unlock(&lock);
   }
   errno = e;
   return r;
}
