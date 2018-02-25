#ifndef __VKDF_SHADER_H__
#define __VKDF_SHADER_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"

uint32_t *vkdf_shader_read_spirv_file(const char *path, VkDeviceSize *size);

VkShaderModule vkdf_create_shader_module(VkdfContext *ctx, const char *path);

#endif
