/*
 * snes_vulkan.c: the SNES's picture on the Vulkan backend (the kit's
 * rr_vulkan.c, through rr_vulkan_ext.h): its compositor (compose_body.h,
 * shaders/compose.comp and compose_rec.comp) in compute pipelines, and a
 * frame with Mode 7 with rr_mode7.h's arithmetic in a fragment shader
 * (shaders/m7.frag). The pipelines, sets and samplers are made at start-up,
 * in the backend's open hook (snes_vulkan_open).
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes_rr.h"
#include "rr_mode7.h"
#include "nk/rr_vulkan_ext.h"
#include "rr_spv_snes.h"

#define VK_OK(x) rr_vk_check((x), #x)

/* a frame with Mode 7 presented (rr_mode7.h, shaders/m7.frag): its data
   in the ring, read as a storage buffer, the planes in an image array */
static VkDescriptorSetLayout s_m7lay;
static VkPipelineLayout s_m7playout;
static VkDescriptorPool s_m7pool;
static VkDescriptorSet s_m7set;
static VkPipeline s_m7;
static VkSampler s_ismp;                    /* integer images: nearest */
static VkImage s_plane;
static VkDeviceMemory s_plane_mem;
static VkImageView s_plane_view;
static int s_plane_layers, s_planes, s_plane_fresh;
static uint32_t s_plane_gen;

/* the compositor (compose_body.h, shaders/compose.comp): set 0 the ring
   (binding 0) and the picture it draws (binding 1, a storage image), the
   description's word in the ring a push constant; the picture's set for
   drawing it */
static VkDescriptorSetLayout s_clay;
static VkPipelineLayout s_cplayout;
static VkDescriptorPool s_cpool;
static VkDescriptorSet s_cset, s_cdraw;
static VkPipeline s_cp;
/* the records' compositor (shaders/compose_rec.comp): set 0 the ring alone */
static VkDescriptorSetLayout s_crlay;
static VkPipelineLayout s_crplayout;
static VkDescriptorSet s_crset;
static VkPipeline s_cpr;
static void m7_draw(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s, long opx, long opic);
static VkImage s_cimg;
static VkDeviceMemory s_cimg_mem;
static VkImageView s_cimg_view;
static int s_cw, s_ch, s_cimg_fresh;

/* the Mode 7 pipeline: set 0 the ring (binding 0) and the planes
   (binding 1), the push constants the presentation and the data's word
   offsets in the ring */
/* the Mode 7 pass's storage buffer: the ring, as far as the device lets
   one binding reach */
static void ring_bind(void)
{
    VkDescriptorBufferInfo bi;
    VkWriteDescriptorSet wr = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(rr_vk_physical_device(), &pp);
    bi.buffer = rr_vk_ring();
    bi.offset = 0;
    bi.range = rr_vk_ring_size() < pp.limits.maxStorageBufferRange ? rr_vk_ring_size() : pp.limits.maxStorageBufferRange;
    wr.dstSet = s_m7set;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr.pBufferInfo = &bi;
    vkUpdateDescriptorSets(rr_vk_device(), 1, &wr, 0, NULL);
    if (s_cset) {
        wr.dstSet = s_cset;
        vkUpdateDescriptorSets(rr_vk_device(), 1, &wr, 0, NULL);
    }
    if (s_crset) {
        wr.dstSet = s_crset;
        vkUpdateDescriptorSets(rr_vk_device(), 1, &wr, 0, NULL);
    }
}

/* the compositor's pipeline and sets (its picture made by cp_image) */
static int cp_make(void)
{
    VkDevice s_dev = rr_vk_device();
    VkDescriptorSetLayoutBinding b[2] = { { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
                                          { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPushConstantRange pc = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
    VkPipelineLayoutCreateInfo pi = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize ps[3] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
                                   { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
    VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo da = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkComputePipelineCreateInfo ci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    li.bindingCount = 2;
    li.pBindings = b;
    if (!VK_OK(vkCreateDescriptorSetLayout(s_dev, &li, NULL, &s_clay)))
        return 0;
    pi.setLayoutCount = 1;
    pi.pSetLayouts = &s_clay;
    pi.pushConstantRangeCount = 1;
    pi.pPushConstantRanges = &pc;
    if (!VK_OK(vkCreatePipelineLayout(s_dev, &pi, NULL, &s_cplayout)))
        return 0;
    dpi.maxSets = 3;
    ps[0].descriptorCount = 2;
    dpi.poolSizeCount = 3;
    dpi.pPoolSizes = ps;
    if (!VK_OK(vkCreateDescriptorPool(s_dev, &dpi, NULL, &s_cpool)))
        return 0;
    da.descriptorPool = s_cpool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &s_clay;
    if (!VK_OK(vkAllocateDescriptorSets(s_dev, &da, &s_cset)))
        return 0;
    da.pSetLayouts = rr_vk_set_layout();
    if (!VK_OK(vkAllocateDescriptorSets(s_dev, &da, &s_cdraw)))
        return 0;
    {
        VkPushConstantRange rpc = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 12 };
        li.bindingCount = 1;
        if (!VK_OK(vkCreateDescriptorSetLayout(s_dev, &li, NULL, &s_crlay)))
            return 0;
        pi.pSetLayouts = &s_crlay;
        pi.pPushConstantRanges = &rpc;
        if (!VK_OK(vkCreatePipelineLayout(s_dev, &pi, NULL, &s_crplayout)))
            return 0;
        da.pSetLayouts = &s_crlay;
        if (!VK_OK(vkAllocateDescriptorSets(s_dev, &da, &s_crset)))
            return 0;
    }
    ring_bind();
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = rr_vk_module(k_compose_comp, sizeof k_compose_comp);
    ci.stage.pName = "main";
    ci.layout = s_cplayout;
    if (!VK_OK(vkCreateComputePipelines(s_dev, VK_NULL_HANDLE, 1, &ci, NULL, &s_cp)))
        return 0;
    ci.stage.module = rr_vk_module(k_compose_rec_comp, sizeof k_compose_rec_comp);
    ci.layout = s_crplayout;
    return VK_OK(vkCreateComputePipelines(s_dev, VK_NULL_HANDLE, 1, &ci, NULL, &s_cpr));
}

/* the compositor's picture, w x h: made again (after what the frame has
   recorded so far has run) when its size changes */
static int cp_image(int w, int h)
{
    VkDescriptorImageInfo di;
    VkWriteDescriptorSet wr[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET } };
    if (w == s_cw && h == s_ch && s_cimg)
        return 1;
    /* the frame so far reads the old picture */
    rr_vk_flush();
    rr_vk_wait_frame();
    vkDeviceWaitIdle(rr_vk_device());
    rr_vk_image_destroy(&s_cimg, &s_cimg_mem, &s_cimg_view);
    s_cw = s_ch = 0;
    if (!rr_vk_image_fmt(w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &s_cimg,
                         &s_cimg_mem, &s_cimg_view))
        return 0;
    di.sampler = rr_vk_sampler();
    di.imageView = s_cimg_view;
    di.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    wr[0].dstSet = s_cset;
    wr[0].dstBinding = 1;
    wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    wr[0].pImageInfo = &di;
    wr[1] = wr[0];
    wr[1].dstSet = s_cdraw;
    wr[1].dstBinding = 0;
    wr[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    {
        VkDescriptorImageInfo ds = di;
        ds.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        wr[1].pImageInfo = &ds;
        vkUpdateDescriptorSets(rr_vk_device(), 2, wr, 0, NULL);
    }
    s_cw = w;
    s_ch = h;
    s_cimg_fresh = 1;
    return 1;
}

static int m7_make(VkShaderModule vs)
{
    VkDevice s_dev = rr_vk_device();
    VkDescriptorSetLayoutBinding b[2] = { { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
                                          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPushConstantRange pc = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 21 * 4 };
    VkPipelineLayoutCreateInfo pi = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
    VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo da = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkSamplerCreateInfo si = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    li.bindingCount = 2;
    li.pBindings = b;
    if (!VK_OK(vkCreateDescriptorSetLayout(s_dev, &li, NULL, &s_m7lay)))
        return 0;
    pi.setLayoutCount = 1;
    pi.pSetLayouts = &s_m7lay;
    pi.pushConstantRangeCount = 1;
    pi.pPushConstantRanges = &pc;
    if (!VK_OK(vkCreatePipelineLayout(s_dev, &pi, NULL, &s_m7playout)))
        return 0;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    if (!VK_OK(vkCreateDescriptorPool(s_dev, &dpi, NULL, &s_m7pool)))
        return 0;
    da.descriptorPool = s_m7pool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &s_m7lay;
    if (!VK_OK(vkAllocateDescriptorSets(s_dev, &da, &s_m7set)))
        return 0;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!VK_OK(vkCreateSampler(s_dev, &si, NULL, &s_ismp)))
        return 0;
    ring_bind();
    s_m7 = rr_vk_make_pipe(vs, rr_vk_module(k_m7_frag, sizeof k_m7_frag), 1, s_m7playout, rr_vk_pass_load());
    return s_m7 != VK_NULL_HANDLE;
}

/* the planes' image, layers deep, made again when it must hold more */
static int m7_plane_image(int layers)
{
    VkDevice s_dev = rr_vk_device();
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkDescriptorImageInfo di;
    VkWriteDescriptorSet wr = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    if (s_plane && s_plane_layers >= layers)
        return 1;
    if (s_plane) {
        vkDeviceWaitIdle(s_dev);
        rr_vk_image_destroy(&s_plane, &s_plane_mem, &s_plane_view);
    }
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8_UINT;
    ii.extent.width = ii.extent.height = 1024;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = (uint32_t)layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!VK_OK(vkCreateImage(s_dev, &ii, NULL, &s_plane)) || !rr_vk_alloc_bind_image(s_plane, &s_plane_mem))
        return 0;
    vi.image = s_plane;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = VK_FORMAT_R8_UINT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = (uint32_t)layers;
    if (!VK_OK(vkCreateImageView(s_dev, &vi, NULL, &s_plane_view)))
        return 0;
    di.sampler = s_ismp;
    di.imageView = s_plane_view;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    wr.dstSet = s_m7set;
    wr.dstBinding = 1;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    vkUpdateDescriptorSets(s_dev, 1, &wr, 0, NULL);
    s_plane_layers = layers;
    s_plane_fresh = 1;
    s_planes = 0;
    return 1;
}

/* n bytes into the ring: their offset in words, or -1 */
static long ring_put(const void *p, size_t n)
{
    long o = rr_vk_ring_take(n ? n : 4);
    if (o >= 0 && n)
        memcpy(rr_vk_ring_map() + o, p, n);
    return o < 0 ? -1 : o / 4;
}


int rr_compose_mode7(const uint32_t *desc, size_t words, const struct RrMode7 *m, const int d[4],
                     const struct RrM7Samples *s)
{
    size_t np = (size_t)m->pw * (size_t)m->ph;
    int ow = d[2] - d[0], oh = d[3] - d[1];
    long off, orec, opic;
    uint32_t pc[3];
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    if (!rr_vk_recording() || !s_cpr || np == 0 || ow <= 0 || oh <= 0)
        return 0;
    /* room for the description, the records and picture, and everything
       the presentation puts in the ring after them (m7_draw's), at once:
       no flush between them */
    if (!rr_vk_ring_fit(words * 4 + np * 20 + (VkDeviceSize)m->vrams * 0x110000u + (size_t)m->segs * sizeof *m->seg +
                        (size_t)m->pals * 512 + (size_t)ow * 12 + (size_t)oh * 16 + 56 * 4 + (m->cv ? m->cv_len : 0) +
                        6 * 16 + 16 * 256))
        return 0;
    if ((off = ring_put(desc, words * 4)) < 0 || (orec = rr_vk_ring_take(np * 16)) < 0 || (opic = rr_vk_ring_take(np * 4)) < 0)
        return 0;
    pc[0] = (uint32_t)off;
    pc[1] = (uint32_t)(orec / 4);
    pc[2] = (uint32_t)(opic / 4);
    vkCmdBindPipeline(rr_vk_cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, s_cpr);
    vkCmdBindDescriptorSets(rr_vk_cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, s_crplayout, 0, 1, &s_crset, 0, NULL);
    vkCmdPushConstants(rr_vk_cmd(), s_crplayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vkCmdDispatch(rr_vk_cmd(), (uint32_t)(m->pw + 31) / 32, (uint32_t)(m->ph + 7) / 8, 1);
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(rr_vk_cmd(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0,
                         NULL, 0, NULL);
    m7_draw(m, d, s, orec / 4, opic / 4);
    return 1;
}

void rr_mode7(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s) { m7_draw(m, d, s, -1, -1); }

/* m presented over d; its records and picture at words opx and opic of
   the ring where given (the compositor's, >= 0), else m's own */
static void m7_draw(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s, long opx, long opic)
{
    static const int ix[6] = { 0, 1, 2, 2, 1, 3 };
    int ow = d[2] - d[0], oh = d[3] - d[1], k;
    size_t np = (size_t)m->pw * (size_t)m->ph;
    int32_t pcv[21], uv[56];
    long off[9], qo;
    float c[4][4] = { { (float)d[0], (float)d[1], 0, 0 }, { (float)d[2], (float)d[1], 1, 0 },
                      { (float)d[0], (float)d[3], 0, 1 }, { (float)d[2], (float)d[3], 1, 1 } };
    float *q;
    VkDeviceSize vo;
    VkBuffer ring;
    int rw, rh;
    if (!rr_vk_recording() || ow <= 0 || oh <= 0 || np == 0)
        return;
    if (!m7_plane_image(m->vrams ? m->vrams : 1))
        return;
    {
        /* everything this draw puts in the ring, each part aligned */
        VkDeviceSize need = (VkDeviceSize)m->vrams * 0x100000u + (opx < 0 ? np * sizeof *m->px : 0) +
                            (size_t)m->segs * sizeof *m->seg + (size_t)m->pals * 512 + (size_t)m->vrams * 0x10000 +
                            (opic < 0 ? np * 4 : 0) + (size_t)ow * 12 +
                            (size_t)oh * 16 + sizeof uv + (m->cv ? m->cv_len : 0) + 6 * 16 + 12 * 256;
        if (!rr_vk_ring_fit(need))
            return;
    }
    if (s_planes < m->vrams || s_plane_gen != m->plane_gen) {
        /* the planes changed: copied in from the ring before this frame's
           draws read them */
        long po = rr_vk_ring_take((size_t)m->vrams * 0x100000u);
        VkBufferImageCopy cp;
        if (po < 0)
            return;
        memcpy(rr_vk_ring_map() + po, m->plane, (size_t)m->vrams * 0x100000u);
        {
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            b.oldLayout = s_plane_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = s_plane;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.layerCount = (uint32_t)s_plane_layers;
            vkCmdPipelineBarrier(rr_vk_cmd(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                                 NULL, 1, &b);
            memset(&cp, 0, sizeof cp);
            cp.bufferOffset = (VkDeviceSize)po;
            cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            cp.imageSubresource.layerCount = (uint32_t)m->vrams;
            cp.imageExtent.width = cp.imageExtent.height = 1024;
            cp.imageExtent.depth = 1;
            if (m->vrams)
                vkCmdCopyBufferToImage(rr_vk_cmd(), rr_vk_ring(), s_plane, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(rr_vk_cmd(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                                 NULL, 1, &b);
        }
        s_plane_fresh = 0;
        s_plane_gen = m->plane_gen;
        s_planes = m->vrams;
    }
    /* the supersamples' offsets: 4 from 0, 8 from 4, 16 from 12; then dv */
    memcpy(uv, s->du4, sizeof s->du4);
    memcpy(uv + 4, s->du8, sizeof s->du8);
    memcpy(uv + 12, s->du16, sizeof s->du16);
    memcpy(uv + 28, s->dv4, sizeof s->dv4);
    memcpy(uv + 32, s->dv8, sizeof s->dv8);
    memcpy(uv + 40, s->dv16, sizeof s->dv16);
    off[0] = opx >= 0 ? opx : ring_put(m->px, np * sizeof *m->px);
    off[1] = ring_put(m->seg, (size_t)m->segs * sizeof *m->seg);
    off[2] = ring_put(m->pal, (size_t)m->pals * 512);
    off[3] = ring_put(m->vram, (size_t)m->vrams * 0x10000);
    off[4] = opic >= 0 ? opic : ring_put(m->pic, np * 4);
    off[5] = ring_put(s->cols, (size_t)ow * 3 * 4);
    off[6] = ring_put(s->rows, (size_t)oh * 4 * 4);
    off[7] = ring_put(uv, sizeof uv);
    {
        /* the canvas's texels, padded to whole words with 0 (a word where
           there is none) */
        size_t n = m->cv ? m->cv_len : 0, n4 = (n + 3) & ~(size_t)3;
        long o = rr_vk_ring_take(n4 ? n4 : 4);
        if (o >= 0) {
            memset(rr_vk_ring_map() + o, 0, n4 ? n4 : 4);
            if (n)
                memcpy(rr_vk_ring_map() + o, m->cv, n);
        }
        off[8] = o < 0 ? -1 : o / 4;
    }
    for (k = 0; k < 9; k++)
        if (off[k] < 0)
            return;
    if ((qo = rr_vk_ring_take(6 * 16)) < 0)
        return;
    q = (float *)(rr_vk_ring_map() + qo);
    rr_vk_target_size(&rw, &rh);
    for (k = 0; k < 6; k++) {
        q[4 * k + 0] = c[ix[k]][0] / rw * 2 - 1;
        q[4 * k + 1] = 1 - c[ix[k]][1] / rh * 2;
        q[4 * k + 2] = c[ix[k]][2];
        q[4 * k + 3] = c[ix[k]][3];
    }
    pcv[0] = m->pw;
    pcv[1] = m->ph;
    pcv[2] = d[0];
    pcv[3] = d[1];
    pcv[4] = s->step_u;
    pcv[5] = s->step_v;
    for (k = 0; k < 8; k++)
        pcv[6 + k] = (int32_t)off[k];
    pcv[14] = m->cpp;
    pcv[15] = m->left;
    pcv[16] = m->cv_k;
    pcv[17] = (int32_t)m->cv_len;
    pcv[18] = (int32_t)off[8];
    pcv[19] = m->cv_w;
    pcv[20] = m->cv_h;
    rr_vk_pass_begin();
    vkCmdBindPipeline(rr_vk_cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, s_m7);
    vkCmdBindDescriptorSets(rr_vk_cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, s_m7playout, 0, 1, &s_m7set, 0, NULL);
    vkCmdPushConstants(rr_vk_cmd(), s_m7playout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pcv, pcv);
    vo = (VkDeviceSize)qo;
    ring = rr_vk_ring();
    vkCmdBindVertexBuffers(rr_vk_cmd(), 0, 1, &ring, &vo);
    vkCmdDraw(rr_vk_cmd(), 6, 1, 0, 0);
    vkCmdEndRenderPass(rr_vk_cmd());
}


static int compose(const uint32_t *desc, size_t words, int w, int h, const uint32_t wp[3], const int d[4]);

int rr_compose(const uint32_t *desc, size_t words, int w, int h, const int d[4])
{
    static const uint32_t none[3] = { 0, 0, 0 };
    return compose(desc, words, w, h, none, d);
}

int rr_compose_woven(const uint32_t *desc, size_t words, int ww, int wh, uint32_t base0, uint32_t base1,
                     const int d[4])
{
    uint32_t wp[3] = { (uint32_t)ww, base0, base1 };
    return compose(desc, words, ww, wh, wp, d);
}

static int compose(const uint32_t *desc, size_t words, int w, int h, const uint32_t wp[3], const int d[4])
{
    long off;
    uint32_t pc[4];
    if (!rr_vk_recording() || !s_cp || w <= 0 || h <= 0 || d[2] <= d[0] || d[3] <= d[1])
        return 0;
    if (!cp_image(w, h) || !rr_vk_ring_fit(words * 4 + 6 * 16 + 512) || (off = ring_put(desc, words * 4)) < 0)
        return 0;
    pc[0] = (uint32_t)off;
    pc[1] = wp[0];
    pc[2] = wp[1];
    pc[3] = wp[2];
    /* the picture drawn by the compositor, then sampled as rr_image's is */
    rr_vk_barrier(rr_vk_cmd(), s_cimg, s_cimg_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    s_cimg_fresh = 0;
    vkCmdBindPipeline(rr_vk_cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, s_cp);
    vkCmdBindDescriptorSets(rr_vk_cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, s_cplayout, 0, 1, &s_cset, 0, NULL);
    vkCmdPushConstants(rr_vk_cmd(), s_cplayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vkCmdDispatch(rr_vk_cmd(), (uint32_t)(w + 31) / 32, (uint32_t)(h + 7) / 8, 1);
    rr_vk_barrier(rr_vk_cmd(), s_cimg, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    rr_vk_draw(s_cdraw, w, h, d);
    return 1;
}


/* the compositor's and Mode 7's pipelines, sets and sampler, made when the
   backend opens (rr_set_open_hook, set by nshvc_main), after its own; the
   ring's sets bound again when it is made again (rr_vk_set_ring_hook) */
int snes_vulkan_open(void)
{
    rr_vk_set_ring_hook(ring_bind);
    if (!m7_make(rr_vk_quad_vs()))
        return 0;
    if (!cp_make())
        return 0;
    return 1;
}
