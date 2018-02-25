#include "vkdf-shader.hpp"

uint32_t *
vkdf_shader_read_spirv_file(const char *path, VkDeviceSize *size)
{
   FILE *pf = fopen(path, "rb");
   if (!pf)
      vkdf_fatal("Could not open SPIR-V file at '%s'", path);

   fseek(pf, 0, SEEK_END);
   VkDeviceSize file_bytes = ftell(pf);
   rewind(pf);

   assert(file_bytes % 4 == 0);
   *size = file_bytes;

   uint8_t *buf = g_new(uint8_t, file_bytes);
   size_t bytes = fread(buf, 1, file_bytes, pf);

   fclose(pf);

   if (bytes != file_bytes)
      vkdf_fatal("Failed to read data from SPIR-V file at '%s'", path);

   return (uint32_t *) buf;
}

VkShaderModule
vkdf_create_shader_module(VkdfContext *ctx, const char *path)
{
   VkDeviceSize size;
   uint32_t *spirv = vkdf_shader_read_spirv_file(path, &size);

   VkShaderModuleCreateInfo mod_info;
   mod_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   mod_info.pNext = NULL;
   mod_info.flags = 0;
   mod_info.codeSize = size;
   mod_info.pCode = spirv;

   VkShaderModule module;
   VK_CHECK(vkCreateShaderModule(ctx->device, &mod_info, NULL, &module));

   g_free(spirv);

   return module;
}

