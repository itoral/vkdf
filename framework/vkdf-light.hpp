#ifndef __VKDF_LIGHT_H__
#define __VKDF_LIGHT_H__

enum {
   VKDF_LIGHT_DIRECTIONAL = 0,
   VKDF_LIGHT_POINT       = 1,
   VKDF_LIGHT_SPOTLIGHT   = 2,
};

typedef struct {
   // Common light attributes
   glm::vec4 origin;      // .w = light type
   glm::vec4 diffuse;
   glm::vec4 ambient;
   glm::vec4 specular;
   glm::vec4 attenuation; // .x = constant, .y = linear, .z = quadratic

   // Spotlights
   struct {
      struct {
         glm::vec4 rot;
         glm::vec4 dir;         // Computed from rotation
      } priv;
      float cutoff;             // cosine of the spotlight's cutoff angle
      float cutoff_angle;       // spotlight's cutoff angle
      float cutoff_padding[2];  // We want this to be vec4-aligned
   } spot;

   uint32_t casts_shadows;
   uint32_t is_dynamic;
   float padding[2];
} VkdfLight;

VkdfLight *
vkdf_light_new_directional(glm::vec4 dir,
                           glm::vec4 diffuse,
                           glm::vec4 ambient,
                           glm::vec4 specular);

VkdfLight *
vkdf_light_new_positional(glm::vec4 pos,
                          glm::vec4 diffuse,
                          glm::vec4 ambient,
                          glm::vec4 specular,
                          glm::vec4 attenuation);

VkdfLight *
vkdf_light_new_spotlight(glm::vec4 pos,
                         float cutoff_angle,
                         glm::vec4 diffuse,
                         glm::vec4 ambient,
                         glm::vec4 specular,
                         glm::vec4 attenuation);

uint32_t inline
vkdf_light_get_type(VkdfLight *l)
{
   return (uint32_t) l->origin.w;
}

void inline
vkdf_light_set_cutoff_angle(VkdfLight *l, float angle)
{
   l->spot.cutoff_angle = angle;
   l->spot.cutoff = cosf(l->spot.cutoff_angle);
}

float inline
vkdf_light_get_cutoff_angle(VkdfLight *l)
{
   return l->spot.cutoff_angle;
}


// WARNING: do not write light.{rot,dir} directly from applications,
// always do it through this function so we update both at once
void inline
vkdf_light_set_rotation(VkdfLight *l, glm::vec3 rot)
{
   l->spot.priv.rot = glm::vec4(rot, 0.0f);
   l->spot.priv.dir =
      glm::vec4(vkdf_compute_viewdir(glm::vec3(l->spot.priv.rot)), 0.0f);
}

void inline
vkdf_light_enable_shadows(VkdfLight *l, bool enable)
{
   l->casts_shadows = (uint32_t) enable;
}

void inline
vkdf_light_look_at(VkdfLight *l, glm::vec3 target)
{
   glm::vec3 rot = vkdf_compute_view_rotation(glm::vec3(l->origin), target);
   vkdf_light_set_rotation(l, rot);
}

glm::mat4 inline
vkdf_light_get_view_matrix(VkdfLight *l)
{
   return vkdf_compute_view_matrix_for_rotation(glm::vec3(l->origin),
                                                glm::vec3(l->spot.priv.rot));
}

bool
vkdf_light_is_box_visible(VkdfLight *l, VkdfBox *box);

void inline
vkdf_light_set_is_dynamic(VkdfLight *l, bool enable)
{
   l->is_dynamic = (uint32_t) enable;
}

bool inline
vkdf_light_is_dynamic(VkdfLight *l)
{
   return l->is_dynamic;
}

void
vkdf_light_free(VkdfLight *light);

#endif
