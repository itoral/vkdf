#ifndef __VKDF_LIGHT_H__
#define __VKDF_LIGHT_H__

typedef struct {
   glm::vec4 origin;
   glm::vec4 diffuse;
   glm::vec4 ambient;
   glm::vec4 specular;
   glm::vec4 attenuation; // .x = constant, .y = linear, .z = quadratic
} VkdfLight;

VkdfLight *
vkdf_light_new_positional(glm::vec4 pos,
                          glm::vec4 diffuse,
                          glm::vec4 ambient,
                          glm::vec4 specular,
                          glm::vec4 attenuation);

void
vkdf_light_free(VkdfLight *light);

#endif
