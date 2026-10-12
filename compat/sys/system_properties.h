/* glibc stand-in for bionic's <sys/system_properties.h>, header-only. Android properties map to
 * environment variables: "debug.radv_xclipse_ctxinit" reads DEBUG_RADV_XCLIPSE_CTXINIT. */
#ifndef RADV540_COMPAT_SYSTEM_PROPERTIES_H
#define RADV540_COMPAT_SYSTEM_PROPERTIES_H

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define PROP_NAME_MAX 32
#define PROP_VALUE_MAX 92

#ifdef __cplusplus
extern "C" {
#endif

static inline int
__system_property_get(const char *name, char *value)
{
   char env[128];
   size_t i = 0;

   value[0] = '\0';
   if (!name)
      return 0;
   for (; name[i] && i < sizeof(env) - 1; i++)
      env[i] = name[i] == '.' ? '_' : (char)toupper((unsigned char)name[i]);
   env[i] = '\0';

   const char *v = getenv(env);
   if (!v)
      return 0;
   strncpy(value, v, PROP_VALUE_MAX - 1);
   value[PROP_VALUE_MAX - 1] = '\0';
   return (int)strlen(value);
}

#ifdef __cplusplus
}
#endif

#endif
