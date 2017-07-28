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
vkdf_box_get_vertex(VkdfBox *box, uint32_t index);

bool
vkdf_box_is_inside(VkdfBox *box, glm::vec3 *p);

bool
vkdf_box_collision(VkdfBox *box1, VkdfBox *box2);

void
vkdf_box_transform(VkdfBox *box, glm::mat4 *transform);

uint32_t
vkdf_box_is_in_frustum(VkdfBox *box, VkdfPlane *fplanes);

#endif
