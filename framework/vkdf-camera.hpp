#ifndef __VKDF_CAMERA_H__
#define __VKDF_CAMERA_H__

typedef struct {
   glm::vec3 pos;
   glm::vec3 rot;
   float dist;
   bool dirty;
} VkdfCamera;

VkdfCamera *
vkdf_camera_new(float px, float py, float pz,
                float rx, float ry, float rz);

void
vkdf_camera_free(VkdfCamera *cam);

glm::vec3
vkdf_camera_get_position(VkdfCamera *cam);

void
vkdf_camera_set_position(VkdfCamera *cam, float px, float py, float pz);

glm::vec3
vkdf_camera_get_rotation (VkdfCamera *cam);

void
vkdf_camera_set_rotation (VkdfCamera *cam, float rx, float ry, float rz);

void
vkdf_camera_move(VkdfCamera *cam, float dx, float dy, float dz);

void
vkdf_camera_rotate(VkdfCamera *cam, float rx, float ry, float rz);

glm::vec3
vkdf_camera_get_viewdir(VkdfCamera *cam);

void
vkdf_camera_step(VkdfCamera *cam, float d, int stepX, int stepY, int stepZ);
void
vkdf_camera_strafe(VkdfCamera *cam, float d);

void
vkdf_camera_look_at(VkdfCamera *cam, float x, float y, float z);

glm::mat4
vkdf_camera_get_view_matrix(VkdfCamera *cam);

glm::mat4
vkdf_camera_get_rotation_matrix(VkdfCamera *cam);

#endif
