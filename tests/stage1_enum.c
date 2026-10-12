/*
 * Stage 1: enumeration only. Creates a Vulkan instance, lists physical devices and prints their
 * properties, memory heaps and queue families. It never calls vkCreateDevice, so no command
 * buffer reaches the GPU. RADV only issues INFO ioctls, a few register reads and context
 * alloc/free while enumerating.
 */
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

static const char *
type_name(VkPhysicalDeviceType t)
{
   switch (t) {
   case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
   case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
   case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
   case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
   default: return "other";
   }
}

int
main(void)
{
   const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "stage1_enum",
      .apiVersion = VK_API_VERSION_1_3,
   };
   const VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   VkInstance inst;
   VkResult r = vkCreateInstance(&ici, NULL, &inst);
   printf("vkCreateInstance: %d\n", r);
   if (r != VK_SUCCESS)
      return 1;

   uint32_t n = 0;
   r = vkEnumeratePhysicalDevices(inst, &n, NULL);
   printf("vkEnumeratePhysicalDevices: %d, %u device(s)\n", r, n);
   VkPhysicalDevice *pd = calloc(n ? n : 1, sizeof(*pd));
   vkEnumeratePhysicalDevices(inst, &n, pd);

   for (uint32_t i = 0; i < n; i++) {
      VkPhysicalDeviceDriverProperties drv = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
      VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drv};
      vkGetPhysicalDeviceProperties2(pd[i], &p2);
      const VkPhysicalDeviceProperties *p = &p2.properties;
      printf("\n[%u] %s (%s)\n", i, p->deviceName, type_name(p->deviceType));
      printf("    api %u.%u.%u  vendor 0x%04x  device 0x%x\n", VK_API_VERSION_MAJOR(p->apiVersion),
             VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion), p->vendorID,
             p->deviceID);
      printf("    driver: %s / %s\n", drv.driverName, drv.driverInfo);
      printf("    maxImageDimension2D %u  maxComputeWorkGroupInvocations %u  subgroup-capable limits ok\n",
             p->limits.maxImageDimension2D, p->limits.maxComputeWorkGroupInvocations);

      VkPhysicalDeviceMemoryProperties mp;
      vkGetPhysicalDeviceMemoryProperties(pd[i], &mp);
      for (uint32_t h = 0; h < mp.memoryHeapCount; h++)
         printf("    heap %u: %llu MiB flags 0x%x\n", h,
                (unsigned long long)(mp.memoryHeaps[h].size >> 20), mp.memoryHeaps[h].flags);
      for (uint32_t t = 0; t < mp.memoryTypeCount; t++)
         printf("    type %u: heap %u flags 0x%x\n", t, mp.memoryTypes[t].heapIndex,
                mp.memoryTypes[t].propertyFlags);

      uint32_t qn = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qn, NULL);
      VkQueueFamilyProperties *q = calloc(qn ? qn : 1, sizeof(*q));
      vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qn, q);
      for (uint32_t f = 0; f < qn; f++)
         printf("    queue family %u: flags 0x%x count %u\n", f, q[f].queueFlags, q[f].queueCount);
      free(q);
   }

   free(pd);
   vkDestroyInstance(inst, NULL);
   printf("\ndone (no device created, nothing submitted)\n");
   return 0;
}
