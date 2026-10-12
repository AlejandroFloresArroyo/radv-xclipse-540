/*
 * Stage 2, one step per run, on a COMPUTE-ONLY queue (compute registers sit at their gfx10.3
 * offsets on this GPU, so the TITAN context-register remap is not involved):
 *   device   create and destroy a VkDevice; submits nothing
 *   empty    one vkQueueSubmit with no command buffers and a fence (at most a tiny no-op IB)
 *   compute  one vkCmdDispatch of 64 invocations writing gid*3+7 into a host-visible buffer
 * Every fence wait is bounded (5 s). On a timeout the process exits without tearing down.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "stage2_fill.h"

#define N 64
#define CHECK(x)                                                                                   \
   do {                                                                                            \
      VkResult _r = (x);                                                                           \
      if (_r != VK_SUCCESS) {                                                                      \
         printf("FAIL %s -> %d\n", #x, _r);                                                        \
         return 1;                                                                                 \
      }                                                                                            \
   } while (0)

static int
wait_fence(VkDevice dev, VkFence fence, const char *what)
{
   VkResult r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
   printf("%s: vkWaitForFences -> %d%s\n", what, r,
          r == VK_SUCCESS ? " (ok)" : r == VK_TIMEOUT ? " (TIMEOUT: GPU did not finish in 5 s)" :
          r == VK_ERROR_DEVICE_LOST ? " (DEVICE_LOST)" : "");
   if (r != VK_SUCCESS) {
      fflush(stdout);
      _exit(2); /* no teardown on a stuck queue */
   }
   return 0;
}

int
main(int argc, char **argv)
{
   const char *step = argc > 1 ? argv[1] : "device";
   const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .pApplicationName = "stage2",
                                  .apiVersion = VK_API_VERSION_1_3};
   const VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                     .pApplicationInfo = &app};
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
   uint32_t n = 1;
   VkPhysicalDevice pd;
   VkResult er = vkEnumeratePhysicalDevices(inst, &n, &pd);
   if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || n == 0) {
      printf("FAIL no physical device (%d)\n", er);
      return 1;
   }

   /* Compute-only family: COMPUTE set, GRAPHICS not set. */
   uint32_t qn = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, NULL);
   VkQueueFamilyProperties qf[8];
   if (qn > 8)
      qn = 8;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
   uint32_t fam = UINT32_MAX;
   for (uint32_t i = 0; i < qn; i++)
      if ((qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
         fam = i;
         break;
      }
   if (fam == UINT32_MAX) {
      printf("FAIL no compute-only queue family\n");
      return 1;
   }
   printf("step=%s compute-only queue family=%u\n", step, fam);

   const float prio = 1.0f;
   const VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                        .queueFamilyIndex = fam,
                                        .queueCount = 1,
                                        .pQueuePriorities = &prio};
   const VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                   .queueCreateInfoCount = 1,
                                   .pQueueCreateInfos = &qci};
   VkDevice dev;
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   printf("vkCreateDevice: ok\n");
   VkQueue q;
   vkGetDeviceQueue(dev, fam, 0, &q);

   if (!strcmp(step, "device")) {
      vkDestroyDevice(dev, NULL);
      vkDestroyInstance(inst, NULL);
      printf("PASS device (nothing submitted)\n");
      return 0;
   }

   const VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));

   if (!strcmp(step, "empty")) {
      CHECK(vkQueueSubmit(q, 0, NULL, fence));
      wait_fence(dev, fence, "empty submit");
      vkDestroyFence(dev, fence, NULL);
      vkDestroyDevice(dev, NULL);
      vkDestroyInstance(inst, NULL);
      printf("PASS empty\n");
      return 0;
   }

   /* compute: host-visible + coherent buffer of N uints, prefilled with a sentinel. */
   const VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                   .size = N * sizeof(uint32_t),
                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   VkBuffer buf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
   VkMemoryRequirements mr;
   vkGetBufferMemoryRequirements(dev, buf, &mr);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   uint32_t mt = UINT32_MAX;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
         mt = i;
         break;
      }
   if (mt == UINT32_MAX) {
      printf("FAIL no host-visible coherent memory type\n");
      return 1;
   }
   const VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = mr.size,
                                     .memoryTypeIndex = mt};
   VkDeviceMemory mem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &mem));
   CHECK(vkBindBufferMemory(dev, buf, mem, 0));
   uint32_t *map;
   CHECK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&map));
   for (int i = 0; i < N; i++)
      map[i] = 0xDEADBEEF;
   printf("buffer: %llu bytes, memory type %u (flags 0x%x)\n", (unsigned long long)mr.size, mt,
          mp.memoryTypes[mt].propertyFlags);

   const VkDescriptorSetLayoutBinding b = {.binding = 0,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
   const VkDescriptorSetLayoutCreateInfo dslci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                  .bindingCount = 1,
                                                  .pBindings = &b};
   VkDescriptorSetLayout dsl;
   CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   const VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                            .setLayoutCount = 1,
                                            .pSetLayouts = &dsl};
   VkPipelineLayout pl;
   CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
   const VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                          .codeSize = sizeof(stage2_fill_spv),
                                          .pCode = stage2_fill_spv};
   VkShaderModule sm;
   CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
   const VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = sm,
                .pName = "main"},
      .layout = pl,
   };
   VkPipeline pipe;
   CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe));
   printf("compute pipeline: compiled (ACO)\n");

   const VkDescriptorPoolSize ps = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
   const VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                            .maxSets = 1,
                                            .poolSizeCount = 1,
                                            .pPoolSizes = &ps};
   VkDescriptorPool dp;
   CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dp));
   const VkDescriptorSetAllocateInfo dsai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                             .descriptorPool = dp,
                                             .descriptorSetCount = 1,
                                             .pSetLayouts = &dsl};
   VkDescriptorSet ds;
   CHECK(vkAllocateDescriptorSets(dev, &dsai, &ds));
   const VkDescriptorBufferInfo dbi = {.buffer = buf, .offset = 0, .range = VK_WHOLE_SIZE};
   const VkWriteDescriptorSet wds = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstSet = ds,
                                     .dstBinding = 0,
                                     .descriptorCount = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     .pBufferInfo = &dbi};
   vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

   const VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                        .queueFamilyIndex = fam};
   VkCommandPool cp;
   CHECK(vkCreateCommandPool(dev, &cpi, NULL, &cp));
   const VkCommandBufferAllocateInfo cbai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                             .commandPool = cp,
                                             .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                             .commandBufferCount = 1};
   VkCommandBuffer cb;
   CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));
   const VkCommandBufferBeginInfo cbbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
   CHECK(vkBeginCommandBuffer(cb, &cbbi));
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
   vkCmdDispatch(cb, 1, 1, 1);
   const VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL,
                        0, NULL);
   CHECK(vkEndCommandBuffer(cb));

   const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
   CHECK(vkQueueSubmit(q, 1, &si, fence));
   wait_fence(dev, fence, "compute dispatch");

   int bad = 0;
   for (int i = 0; i < N; i++)
      if (map[i] != (uint32_t)(i * 3 + 7)) {
         if (bad < 8)
            printf("  mismatch [%d] = 0x%08x (want %u)\n", i, map[i], i * 3 + 7);
         bad++;
      }
   printf("readback: %d/%d correct (first values: %u %u %u ... last %u)\n", N - bad, N, map[0], map[1], map[2],
          map[N - 1]);

   vkDestroyCommandPool(dev, cp, NULL);
   vkDestroyDescriptorPool(dev, dp, NULL);
   vkDestroyPipeline(dev, pipe, NULL);
   vkDestroyShaderModule(dev, sm, NULL);
   vkDestroyPipelineLayout(dev, pl, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   vkUnmapMemory(dev, mem);
   vkFreeMemory(dev, mem, NULL);
   vkDestroyBuffer(dev, buf, NULL);
   vkDestroyFence(dev, fence, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   printf(bad ? "FAIL compute (wrong results)\n" : "PASS compute\n");
   return bad ? 3 : 0;
}
