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
      float cutoff;             // cosine of the spotlight's cutoff angle (half of apeture angle)
      float cutoff_angle;       // spotlight's cutoff angle (half of aperture angle)
      float cutoff_padding[2];  // We want this to be vec4-aligned
   } spot;

   uint32_t casts_shadows;
   uint32_t is_dynamic;
   uint32_t dirty;
   float padding[1];
} VkdfLight;

#define SET_FIELD(light, field, value) \
{                                      \
   field = value;                      \
   light->dirty = (uint32_t) true;     \
}

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

void inline
vkdf_light_set_type(VkdfLight *l, uint32_t light_type)
{
   l->origin.w = (float) light_type;
}

uint32_t inline
vkdf_light_get_type(VkdfLight *l)
{
   return (uint32_t) l->origin.w;
}

void inline
vkdf_light_set_position(VkdfLight *l, glm::vec3 pos)
{
   assert(vkdf_light_get_type(l) != VKDF_LIGHT_DIRECTIONAL);
   glm::vec4 p = vec4(pos, l->origin.w);
   SET_FIELD(l, l->origin, p);
}

glm::vec4 inline
vkdf_light_get_position(VkdfLight *l)
{
   assert(vkdf_light_get_type(l) != VKDF_LIGHT_DIRECTIONAL);
   return l->origin;
}

void inline
vkdf_light_set_direction(VkdfLight *l, glm::vec3 dir)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_DIRECTIONAL);
   glm::vec4 d = vec4(dir, l->origin.w);
   SET_FIELD(l, l->origin, d);
}

glm::vec4 inline
vkdf_light_get_direction(VkdfLight *l)
{
   uint32_t light_type = vkdf_light_get_type(l);
   if (light_type == VKDF_LIGHT_DIRECTIONAL)
      return l->origin;
   else if (light_type == VKDF_LIGHT_SPOTLIGHT)
      return l->spot.priv.dir;
   assert(!"Light type does not have a direction vector");
}

void inline
vkdf_light_set_diffuse(VkdfLight *l, glm::vec4 color)
{
   SET_FIELD(l, l->diffuse, color);
}

glm::vec4 inline
vkdf_light_get_diffuse(VkdfLight *l)
{
   return l->diffuse;
}

void inline
vkdf_light_set_ambient(VkdfLight *l, glm::vec4 color)
{
   SET_FIELD(l, l->ambient, color);
}

glm::vec4 inline
vkdf_light_get_ambient(VkdfLight *l)
{
   return l->ambient;
}

void inline
vkdf_light_set_specular(VkdfLight *l, glm::vec4 color)
{
   SET_FIELD(l, l->specular, color);
}

glm::vec4 inline
vkdf_light_get_specular(VkdfLight *l)
{
   return l->specular;
}

void inline
vkdf_light_set_attenuation(VkdfLight *l, glm::vec4 attenuation)
{
   SET_FIELD(l, l->attenuation, attenuation);
}

glm::vec4 inline
vkdf_light_get_attenuation(VkdfLight *l)
{
   return l->attenuation;
}

void inline
vkdf_light_set_cutoff_angle(VkdfLight *l, float angle)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   SET_FIELD(l, l->spot.cutoff_angle, angle);
   SET_FIELD(l, l->spot.cutoff, cosf(l->spot.cutoff_angle));
}

/* The cutoff angle is half of the aperture angle of the spotlight */
void inline
vkdf_light_set_aperture_angle(VkdfLight *l, float angle)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   angle /= 2.0f;
   SET_FIELD(l, l->spot.cutoff_angle, angle);
   SET_FIELD(l, l->spot.cutoff, cosf(l->spot.cutoff_angle));
}

float inline
vkdf_light_get_cutoff_angle(VkdfLight *l)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   return l->spot.cutoff_angle;
}

float inline
vkdf_light_get_aperture_angle(VkdfLight *l)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   return 2.0f * l->spot.cutoff_angle;
}

float inline
vkdf_light_get_cutoff_factor(VkdfLight *l)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   return l->spot.cutoff;
}

// WARNING: do not write light.{rot,dir} directly from applications,
// always do it through this function so we update both at once
void inline
vkdf_light_set_rotation(VkdfLight *l, glm::vec3 rot)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   SET_FIELD(l, l->spot.priv.rot, glm::vec4(rot, 0.0f));
   SET_FIELD(l, l->spot.priv.dir,
             glm::vec4(vkdf_compute_viewdir(glm::vec3(l->spot.priv.rot)), 0.0f));
}

glm::vec3 inline
vkdf_light_get_rotation(VkdfLight *l)
{
   assert(vkdf_light_get_type(l) == VKDF_LIGHT_SPOTLIGHT);
   return l->spot.priv.rot;
}

void inline
vkdf_light_enable_shadows(VkdfLight *l, bool enable)
{
   SET_FIELD(l, l->casts_shadows, (uint32_t) enable);
}

bool inline
vkdf_light_casts_shadows(VkdfLight *l)
{
   return (bool) l->casts_shadows;
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

void inline
vkdf_light_set_is_dynamic(VkdfLight *l, bool enable)
{
   SET_FIELD(l, l->is_dynamic, (uint32_t) enable);
}

bool inline
vkdf_light_is_dynamic(VkdfLight *l)
{
   return l->is_dynamic;
}

void inline
vkdf_light_set_dirty(VkdfLight *l, bool dirty)
{
   l->dirty = (uint32_t) dirty;
}

bool inline
vkdf_light_is_dirty(VkdfLight *l)
{
   return (bool) l->dirty;
}

void
vkdf_light_free(VkdfLight *light);

#undef SET_FIELD

#endif
