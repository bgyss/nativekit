/*
 * rr_vulkan_ext.h: the Vulkan backend's device, frame and helpers, for a
 * console's own drawing on the GPU (its compositor, its own pictures). Its
 * pipelines, sets and images are made in the backend's open hook
 * (rr_set_open_hook), after the backend's own; the frame's commands go
 * into rr_vk_cmd between the backend's passes, each draw a pass of its own
 * (rr_vk_pass_begin, ended by the console).
 */
#ifndef NK_RR_VULKAN_EXT_H
#define NK_RR_VULKAN_EXT_H

#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* before rr_open: the instance's application name ("nativekit" when not set) */
void rr_vk_set_app_name(const char *name);
/* fn called when the staging ring is made again (larger), for the
   console's sets that read it to be written again */
void rr_vk_set_ring_hook(void (*fn)(void));

/* r == VK_SUCCESS; else "rr: vulkan <what>: <r>" printed */
int rr_vk_check(VkResult r, const char *what);

VkDevice rr_vk_device(void);
VkPhysicalDevice rr_vk_physical_device(void);
VkCommandBuffer rr_vk_cmd(void);              /* the frame's command buffer */
int rr_vk_recording(void);                    /* 1 between rr_begin and rr_present */
void rr_vk_target_size(int *w, int *h);       /* the offscreen target's size */
VkRenderPass rr_vk_pass_load(void);           /* a pass over the target that loads it */
const VkDescriptorSetLayout *rr_vk_set_layout(void); /* a picture's set: binding 0, a sampled image */
VkSampler rr_vk_sampler(void);                /* bilinear, clamped: the pictures' */
VkShaderModule rr_vk_quad_vs(void);           /* the quad's vertex shader (a: xy in NDC, uv) */

/* the staging ring (host visible, a transfer source, vertex and storage
   buffer), made again by rr_vk_ring_fit: its handle and mapping are read
   again after that */
VkBuffer rr_vk_ring(void);
uint8_t *rr_vk_ring_map(void);
VkDeviceSize rr_vk_ring_size(void);
long rr_vk_ring_take(VkDeviceSize n);         /* n bytes of the ring, 256 aligned: their offset, or -1 */
int rr_vk_ring_fit(VkDeviceSize n);           /* room for n more bytes this frame; 0 when there cannot be */
void rr_vk_flush(void);                       /* the frame so far run, recording begun again on an empty ring */
void rr_vk_wait_frame(void);                  /* the frame submitted last, finished */

void rr_vk_pass_begin(void);                  /* a pass over the target begun (the frame's first clears it) */
/* the image of set (w x h) drawn over target rectangle d in a pass of its
   own: one to one when d is w x h, else sharp bilinear */
void rr_vk_draw(VkDescriptorSet set, int w, int h, const int d[4]);

VkShaderModule rr_vk_module(const uint32_t *code, size_t size);
/* a graphics pipeline on pass (quad = 1: quads from the ring, a vec4 a
   vertex; quad = 0: no vertex input) */
VkPipeline rr_vk_make_pipe(VkShaderModule vs, VkShaderModule fs, int quad, VkPipelineLayout layout, VkRenderPass pass);
int rr_vk_alloc_bind_image(VkImage img, VkDeviceMemory *mem);   /* device-local memory for img */
int rr_vk_image_fmt(int w, int h, VkFormat fmt, VkImageUsageFlags usage, VkImage *img, VkDeviceMemory *mem,
                    VkImageView *view);
void rr_vk_image_destroy(VkImage *img, VkDeviceMemory *mem, VkImageView *view);
void rr_vk_barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src_acc,
                   VkAccessFlags dst_acc, VkPipelineStageFlags src_st, VkPipelineStageFlags dst_st);

#ifdef __cplusplus
}
#endif
#endif
