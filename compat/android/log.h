/* glibc stand-in for <android/log.h>, header-only. The radv-xclipse fork includes this header
 * unconditionally; on Linux its messages go to stderr. INFO and below print only when
 * XCLIPSE_STDERR_LOG=1 (the fork's own RADV_XCLIPSE_LOG level still gates its trace lines). */
#ifndef RADV540_COMPAT_ANDROID_LOG_H
#define RADV540_COMPAT_ANDROID_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum android_LogPriority {
   ANDROID_LOG_UNKNOWN = 0,
   ANDROID_LOG_DEFAULT,
   ANDROID_LOG_VERBOSE,
   ANDROID_LOG_DEBUG,
   ANDROID_LOG_INFO,
   ANDROID_LOG_WARN,
   ANDROID_LOG_ERROR,
   ANDROID_LOG_FATAL,
   ANDROID_LOG_SILENT,
} android_LogPriority;

static inline int
radv540_log_enabled(int prio)
{
   if (prio >= ANDROID_LOG_WARN)
      return 1;
   const char *e = getenv("XCLIPSE_STDERR_LOG");
   return e && e[0] == '1';
}

static inline int
__android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
   if (!radv540_log_enabled(prio))
      return 0;
   fprintf(stderr, "[%s] ", tag ? tag : "?");
   int n = vfprintf(stderr, fmt, ap);
   fputc('\n', stderr);
   return n;
}

static inline int __attribute__((format(printf, 3, 4)))
__android_log_print(int prio, const char *tag, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   int n = __android_log_vprint(prio, tag, fmt, ap);
   va_end(ap);
   return n;
}

static inline int
__android_log_write(int prio, const char *tag, const char *msg)
{
   return __android_log_print(prio, tag, "%s", msg);
}

#ifdef __cplusplus
}
#endif

#endif
