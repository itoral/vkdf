#include "vkdf.hpp"

VkDescriptorPool
vkdf_create_descriptor_pool(VkdfContext *ctx,
                            VkDescriptorType type,
                            uint32_t count)
{
   VkDescriptorPoolSize type_count[1];
   type_count[0].type = type;
   type_count[0].descriptorCount = count;

   VkDescriptorPoolCreateInfo pool_ci;
   pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   pool_ci.pNext = NULL;
   pool_ci.maxSets = 8; // Random choice...
   pool_ci.poolSizeCount = 1;
   pool_ci.pPoolSizes = type_count;
   pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

   VkDescriptorPool pool;
   VK_CHECK(vkCreateDescriptorPool(ctx->device, &pool_ci, NULL, &pool));

   return pool;
}

static bool
is_descriptor_buffer(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return true;
   default:
      return false;
   }
}

VkDescriptorSetLayout
vkdf_create_buffer_descriptor_set_layout(VkdfContext *ctx,
                                         uint32_t binding,
                                         uint32_t count,
                                         VkShaderStageFlags stages,
                                         VkDescriptorType type)
{
   assert(count < 16);
   assert(is_descriptor_buffer(type));

   VkDescriptorSetLayoutBinding set_layout_binding[16];
   for (uint32_t i = 0; i < count; i++) {
      set_layout_binding[i].binding = binding + i;
      set_layout_binding[i].descriptorType = type;
      set_layout_binding[i].descriptorCount = 1;
      set_layout_binding[i].stageFlags = stages;
      set_layout_binding[i].pImmutableSamplers = NULL;
   }

   VkDescriptorSetLayoutCreateInfo set_layout_info;
   set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   set_layout_info.pNext = NULL;
   set_layout_info.bindingCount = count;
   set_layout_info.pBindings = set_layout_binding;
   set_layout_info.flags = 0;

   VkDescriptorSetLayout set_layout;
   VK_CHECK(vkCreateDescriptorSetLayout(ctx->device,
                                        &set_layout_info,
                                        NULL,
                                        &set_layout));
   return set_layout;
}

VkDescriptorSetLayout
vkdf_create_sampler_descriptor_set_layout(VkdfContext *ctx,
                                          uint32_t binding,
                                          uint32_t count,
                                          VkShaderStageFlags stages)
{
   assert(count < 16);

   VkDescriptorSetLayoutBinding set_layout_binding[16];
   for (uint32_t i = 0; i < count; i++) {
      set_layout_binding[i].binding = binding + i;
      set_layout_binding[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      set_layout_binding[i].descriptorCount = 1;
      set_layout_binding[i].stageFlags = stages;
      set_layout_binding[i].pImmutableSamplers = NULL;
   }

   VkDescriptorSetLayoutCreateInfo set_layout_info;
   set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   set_layout_info.pNext = NULL;
   set_layout_info.bindingCount = count;
   set_layout_info.pBindings = set_layout_binding;
   set_layout_info.flags = 0;

   VkDescriptorSetLayout set_layout;
   VK_CHECK(vkCreateDescriptorSetLayout(ctx->device,
                                        &set_layout_info,
                                        NULL,
                                        &set_layout));
   return set_layout;
}

void
vkdf_descriptor_set_buffer_update(VkdfContext *ctx,
                                  VkDescriptorSet descriptor,
                                  VkBuffer buffer,
                                  uint32_t binding,
                                  uint32_t count,
                                  VkDeviceSize *offsets,
                                  VkDeviceSize *ranges,
                                  VkDescriptorType type)
{
   assert(count < 16);
   assert(is_descriptor_buffer(type));

   VkDescriptorBufferInfo buffer_info[16];
   for (uint32_t i = 0; i < count; i++) {
      buffer_info[i].buffer = buffer;
      buffer_info[i].offset = offsets[i];
      buffer_info[i].range = ranges[i];
   }

   VkWriteDescriptorSet writes;
   writes.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   writes.pNext = NULL;
   writes.dstSet = descriptor;
   writes.dstBinding = binding;
   writes.dstArrayElement = 0;
   writes.descriptorCount = count;
   writes.descriptorType = type;
   writes.pBufferInfo = buffer_info;
   writes.pImageInfo = NULL;
   writes.pTexelBufferView = NULL;

   vkUpdateDescriptorSets(ctx->device, 1, &writes, 0, NULL);
}

void
vkdf_descriptor_set_sampler_update(VkdfContext *ctx,
                                   VkDescriptorSet descriptor,
                                   VkSampler sampler,
                                   VkImageView view,
                                   VkImageLayout layout,
                                   uint32_t binding,
                                   uint32_t count)
{
   VkDescriptorImageInfo image_info;
   image_info.sampler = sampler;
   image_info.imageView = view;
   image_info.imageLayout = layout;

   VkWriteDescriptorSet writes;
   writes.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   writes.pNext = NULL;
   writes.dstSet = descriptor;
   writes.dstBinding = binding;
   writes.dstArrayElement = 0;
   writes.descriptorCount = count;
   writes.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   writes.pBufferInfo = NULL;
   writes.pImageInfo = &image_info;
   writes.pTexelBufferView = NULL;

   vkUpdateDescriptorSets(ctx->device, 1, &writes, 0, NULL);
}

