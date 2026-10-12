/*
 * Stage 3, one step per run, on the GRAPHICS queue (Xclipse context init + TITAN register remap):
 *   gfxempty  submit an empty command buffer; the GPU only runs the graphics preamble
 *   triangle  clear a 256x256 RGBA8 image to blue, draw one orange triangle (no depth, no vertex
 *             buffers), copy it to a host-visible buffer, check pixels, write stage3.ppm
 * Every fence wait is bounded (5 s). On a timeout the process exits without tearing down.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "stage3_frag.h"
#include "stage3_vert.h"

#define W 256
#define H 256
#define CHECK(x)                                                                                   \
   do {                                                                                            \
      VkResult _r = (x);                                                                           \
      if (_r != VK_SUCCESS) {                                                                      \
         printf("FAIL %s -> %d\n", #x, _r);                                                        \
         return 1;                                                                                 \
      }                                                                                            \
   } while (0)

static void
wait_fence(VkDevice dev, VkFence fence, const char *what)
{
   VkResult r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
   printf("%s: vkWaitForFences -> %d%s\n", what, r,
          r == VK_SUCCESS ? " (ok)" : r == VK_TIMEOUT ? " (TIMEOUT: GPU did not finish in 5 s)" :
          r == VK_ERROR_DEVICE_LOST ? " (DEVICE_LOST)" : "");
   if (r != VK_SUCCESS) {
      fflush(stdout);
      _exit(2);
   }
}

static uint32_t
find_mem(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want)
{
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
         return i;
   return UINT32_MAX;
}

static VkShaderModule
module(VkDevice dev, const uint32_t *code, size_t size)
{
   const VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .codeSize = size,
                                        .pCode = code};
   VkShaderModule m = VK_NULL_HANDLE;
   vkCreateShaderModule(dev, &ci, NULL, &m);
   return m;
}

static void
image_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
              VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
   const VkImageMemoryBarrier2 b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = src_stage,
      .srcAccessMask = src_access,
      .dstStageMask = dst_stage,
      .dstAccessMask = dst_access,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = img,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   const VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                .imageMemoryBarrierCount = 1,
                                .pImageMemoryBarriers = &b};
   vkCmdPipelineBarrier2(cb, &di);
}

int
main(int argc, char **argv)
{
   const char *step = argc > 1 ? argv[1] : "gfxempty";
   const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .pApplicationName = "stage3",
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

   uint32_t qn = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, NULL);
   VkQueueFamilyProperties qf[8];
   if (qn > 8)
      qn = 8;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
   uint32_t fam = UINT32_MAX;
   for (uint32_t i = 0; i < qn; i++)
      if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         fam = i;
         break;
      }
   if (fam == UINT32_MAX) {
      printf("FAIL no graphics queue family\n");
      return 1;
   }
   printf("step=%s graphics queue family=%u\n", step, fam);

   const float prio = 1.0f;
   const VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                        .queueFamilyIndex = fam,
                                        .queueCount = 1,
                                        .pQueuePriorities = &prio};
   VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                           .dynamicRendering = VK_TRUE,
                                           .synchronization2 = VK_TRUE};
   const VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                   .pNext = &f13,
                                   .queueCreateInfoCount = 1,
                                   .pQueueCreateInfos = &qci};
   VkDevice dev;
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   VkQueue q;
   vkGetDeviceQueue(dev, fam, 0, &q);

   const VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));
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
   const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};

   if (!strcmp(step, "gfxempty")) {
      CHECK(vkBeginCommandBuffer(cb, &cbbi));
      CHECK(vkEndCommandBuffer(cb));
      CHECK(vkQueueSubmit(q, 1, &si, fence));
      wait_fence(dev, fence, "graphics preamble only");
      vkDestroyCommandPool(dev, cp, NULL);
      vkDestroyFence(dev, fence, NULL);
      vkDestroyDevice(dev, NULL);
      vkDestroyInstance(inst, NULL);
      printf("PASS gfxempty\n");
      return 0;
   }

   /* Render target. */
   const VkImageCreateInfo imci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                   .imageType = VK_IMAGE_TYPE_2D,
                                   .format = VK_FORMAT_R8G8B8A8_UNORM,
                                   .extent = {W, H, 1},
                                   .mipLevels = 1,
                                   .arrayLayers = 1,
                                   .samples = VK_SAMPLE_COUNT_1_BIT,
                                   .tiling = VK_IMAGE_TILING_OPTIMAL,
                                   .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                   .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
   VkImage img;
   CHECK(vkCreateImage(dev, &imci, NULL, &img));
   VkMemoryRequirements imr;
   vkGetImageMemoryRequirements(dev, img, &imr);
   const VkMemoryAllocateInfo imai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                      .allocationSize = imr.size,
                                      .memoryTypeIndex = find_mem(pd, imr.memoryTypeBits,
                                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
   VkDeviceMemory imem;
   CHECK(vkAllocateMemory(dev, &imai, NULL, &imem));
   CHECK(vkBindImageMemory(dev, img, imem, 0));
   const VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                       .image = img,
                                       .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                       .format = VK_FORMAT_R8G8B8A8_UNORM,
                                       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   VkImageView view;
   CHECK(vkCreateImageView(dev, &ivci, NULL, &view));

   /* Readback buffer. */
   const VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                   .size = W * H * 4,
                                   .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   VkBuffer buf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
   VkMemoryRequirements bmr;
   vkGetBufferMemoryRequirements(dev, buf, &bmr);
   const VkMemoryAllocateInfo bmai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = bmr.size,
      .memoryTypeIndex = find_mem(pd, bmr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
   VkDeviceMemory bmem;
   CHECK(vkAllocateMemory(dev, &bmai, NULL, &bmem));
   CHECK(vkBindBufferMemory(dev, buf, bmem, 0));
   uint8_t *px;
   CHECK(vkMapMemory(dev, bmem, 0, VK_WHOLE_SIZE, 0, (void **)&px));
   memset(px, 0x55, W * H * 4);

   /* Pipeline (dynamic rendering, no vertex input, no depth). */
   VkShaderModule vs = module(dev, stage3_vert_spv, sizeof(stage3_vert_spv));
   VkShaderModule fs = module(dev, stage3_frag_spv, sizeof(stage3_frag_spv));
   const VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vs,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fs,
       .pName = "main"},
   };
   const VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   const VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
   const VkViewport vp = {0, 0, W, H, 0, 1};
   const VkRect2D sc = {{0, 0}, {W, H}};
   const VkPipelineViewportStateCreateInfo vps = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                  .viewportCount = 1,
                                                  .pViewports = &vp,
                                                  .scissorCount = 1,
                                                  .pScissors = &sc};
   const VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f};
   const VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   const VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = 0xf};
   const VkPipelineColorBlendStateCreateInfo cbs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                    .attachmentCount = 1,
                                                    .pAttachments = &cba};
   const VkFormat cfmt = VK_FORMAT_R8G8B8A8_UNORM;
   const VkPipelineRenderingCreateInfo prci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                               .colorAttachmentCount = 1,
                                               .pColorAttachmentFormats = &cfmt};
   const VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   VkPipelineLayout pl;
   CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
   const VkGraphicsPipelineCreateInfo gpci = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                              .pNext = &prci,
                                              .stageCount = 2,
                                              .pStages = stages,
                                              .pVertexInputState = &vi,
                                              .pInputAssemblyState = &ia,
                                              .pViewportState = &vps,
                                              .pRasterizationState = &rs,
                                              .pMultisampleState = &ms,
                                              .pColorBlendState = &cbs,
                                              .layout = pl};
   VkPipeline pipe;
   CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe));
   printf("graphics pipeline: compiled (ACO)\n");

   CHECK(vkBeginCommandBuffer(cb, &cbbi));
   image_barrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
   const VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                          .imageView = view,
                                          .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                          .clearValue = {.color = {.float32 = {0.0f, 0.0f, 1.0f, 1.0f}}}};
   const VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                               .renderArea = {{0, 0}, {W, H}},
                               .layerCount = 1,
                               .colorAttachmentCount = 1,
                               .pColorAttachments = &att};
   vkCmdBeginRendering(cb, &ri);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
   vkCmdDraw(cb, 3, 1, 0, 0);
   vkCmdEndRendering(cb);
   image_barrier(cb, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
   const VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                     .imageExtent = {W, H, 1}};
   vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);
   const VkMemoryBarrier2 hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT};
   const VkDependencyInfo hdi = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .memoryBarrierCount = 1,
                                 .pMemoryBarriers = &hb};
   vkCmdPipelineBarrier2(cb, &hdi);
   CHECK(vkEndCommandBuffer(cb));
   CHECK(vkQueueSubmit(q, 1, &si, fence));
   wait_fence(dev, fence, "triangle");

   /* Inside the triangle: orange (255,128,0). Outside: blue (0,0,255). */
   int orange = 0, blue = 0, other = 0;
   for (int i = 0; i < W * H; i++) {
      const uint8_t *p = px + i * 4;
      if (p[0] == 255 && p[1] >= 126 && p[1] <= 129 && p[2] == 0)
         orange++;
      else if (p[0] == 0 && p[1] == 0 && p[2] == 255)
         blue++;
      else
         other++;
   }
   const uint8_t *c = px + (160 * W + 128) * 4, *k = px + (4 * W + 4) * 4;
   printf("pixels: orange %d, blue %d, other %d (a correct triangle covers about 28%%)\n", orange, blue, other);
   printf("center(128,160) = %u,%u,%u,%u   corner(4,4) = %u,%u,%u,%u\n", c[0], c[1], c[2], c[3], k[0], k[1], k[2],
          k[3]);
   FILE *f = fopen("stage3.ppm", "wb");
   if (f) {
      fprintf(f, "P6 %d %d 255\n", W, H);
      for (int i = 0; i < W * H; i++)
         fwrite(px + i * 4, 1, 3, f);
      fclose(f);
      printf("wrote stage3.ppm\n");
   }

   vkUnmapMemory(dev, bmem);
   vkDestroyPipeline(dev, pipe, NULL);
   vkDestroyPipelineLayout(dev, pl, NULL);
   vkDestroyShaderModule(dev, vs, NULL);
   vkDestroyShaderModule(dev, fs, NULL);
   vkDestroyImageView(dev, view, NULL);
   vkDestroyImage(dev, img, NULL);
   vkFreeMemory(dev, imem, NULL);
   vkDestroyBuffer(dev, buf, NULL);
   vkFreeMemory(dev, bmem, NULL);
   vkDestroyCommandPool(dev, cp, NULL);
   vkDestroyFence(dev, fence, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   const int ok = orange > W * H / 5 && orange < W * H / 2 && blue > W * H / 3 && other < W * H / 50;
   printf(ok ? "PASS triangle\n" : "FAIL triangle (unexpected pixels)\n");
   return ok ? 0 : 3;
}
