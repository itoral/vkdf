#ifndef __VKDF_BOX_H__
#define __VKDF_BOX_H__

typedef struct {
   glm::vec3 center;
   float w, h, d;
} VkdfBox;

enum {
   OUTSIDE = 0,
   INSIDE,
   INTERSECT
};

glm::vec3
vkdf_box_get_vertex(const VkdfBox *box, uint32_t index);

bool
vkdf_box_is_inside(const VkdfBox *box, glm::vec3 *p);

bool
vkdf_box_collision(const VkdfBox *box1, const VkdfBox *box2);

void
vkdf_box_transform(VkdfBox *box, glm::mat4 *transform);

uint32_t
vkdf_box_is_in_frustum(const VkdfBox *box,
                       const VkdfBox *frustum_box,
                       const VkdfPlane *frustum_planes);

uint32_t
vkdf_box_is_in_cone(const VkdfBox *box,
                    glm::vec3 top, glm::vec3 dir, float cutoff);

#endif
