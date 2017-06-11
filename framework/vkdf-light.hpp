#ifndef __VKDF_LIGHT_H__
#define __VKDF_LIGHT_H__

typedef struct {
   // Common light attributes
   glm::vec4 origin;
   glm::vec4 diffuse;
   glm::vec4 ambient;
   glm::vec4 specular;
   glm::vec4 attenuation; // .x = constant, .y = linear, .z = quadratic

   // Spotlights
   glm::vec4 direction;
   float cutoff;         // cosine of the spotlight's cutoff angle
   float padding[3];     // We want this to be vec4-aligned
} VkdfLight;

VkdfLight *
vkdf_light_new_positional(glm::vec4 pos,
                          glm::vec4 diffuse,
                          glm::vec4 ambient,
                          glm::vec4 specular,
                          glm::vec4 attenuation);

VkdfLight *
vkdf_light_new_spotlight(glm::vec4 pos,
                         glm::vec4 direction,
                         float cutoff_angle,
                         glm::vec4 diffuse,
                         glm::vec4 ambient,
                         glm::vec4 specular,
                         glm::vec4 attenuation);

void inline
vkdf_light_set_cutoff_angle(VkdfLight *l, float angle)
{
   l->cutoff = cosf(angle);
}

float inline
vkdf_light_get_cutoff_angle(VkdfLight *l)
{
   return acosf(l->cutoff);
}

void
vkdf_light_free(VkdfLight *light);

#endif
