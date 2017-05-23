#include "vkdf.hpp"

static void
init_light(VkdfLight *l,
           glm::vec4 diffuse,
           glm::vec4 ambient,
           glm::vec4 specular,
           glm::vec4 attenuation)
{
   l->diffuse = diffuse;
   l->ambient = ambient;
   l->specular = specular;
   l->attenuation = attenuation;
}

VkdfLight *
vkdf_light_new_positional(glm::vec4 pos,
                          glm::vec4 diffuse,
                          glm::vec4 ambient,
                          glm::vec4 specular,
                          glm::vec4 attenuation)
{
   assert(pos.w == 1.0f);
   VkdfLight *l = g_new0(VkdfLight, 1);
   init_light(l, diffuse, ambient, specular, attenuation);
   l->origin = pos;
   return l;
}

void
vkdf_light_free(VkdfLight *light)
{
   g_free(light);
}
