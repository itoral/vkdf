#ifndef __VKDF_CAMERA_H__
#define __VKDF_CAMERA_H__

typedef struct {
   struct {
      float fov;
      float near_plane;
      float far_plane;
      float aspect_ratio;
      glm::mat4 matrix;
   } proj;
   glm::vec3 pos;
   glm::vec3 rot;
   float dist;
   bool dirty;
} VkdfCamera;

enum {
  FRUSTUM_FTR = 0,
  FRUSTUM_FTL,
  FRUSTUM_FBR,
  FRUSTUM_FBL,
  FRUSTUM_NTR,
  FRUSTUM_NTL,
  FRUSTUM_NBR,
  FRUSTUM_NBL,
};

VkdfCamera *
vkdf_camera_new(float px, float py, float pz,
                float rx, float ry, float rz);

void
vkdf_camera_free(VkdfCamera *cam);

void
vkdf_camera_set_projection(VkdfCamera *cam,
                           float fov,
                           float near_plane,
                           float far_plane,
                           float aspect_ratio);

inline glm::mat4 *
vkdf_camera_get_projection_ptr(VkdfCamera *cam)
{
   return &cam->proj.matrix;
}

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

inline glm::vec3
vkdf_camera_get_viewdir(VkdfCamera *cam)
{
   return vkdf_compute_viewdir(cam->rot);
}

void
vkdf_camera_step(VkdfCamera *cam, float d, int stepX, int stepY, int stepZ);
void
vkdf_camera_strafe(VkdfCamera *cam, float d);

void
vkdf_camera_look_at(VkdfCamera *cam, float x, float y, float z);

inline bool
vkdf_camera_is_dirty(VkdfCamera *cam)
{
   return cam->dirty;
}

inline void
vkdf_camera_set_dirty(VkdfCamera *cam, bool dirty)
{
   cam->dirty = dirty;
}

inline glm::mat4
vkdf_camera_get_view_matrix(VkdfCamera *cam)
{
   return vkdf_compute_view_matrix_for_rotation(cam->pos, cam->rot);
}

glm::mat4
vkdf_camera_get_rotation_matrix(VkdfCamera *cam);

void
vkdf_camera_get_frustum_vertices_at_distance(VkdfCamera *cam,
                                             float dist,
                                             glm::vec3 *f);

inline void
vkdf_camera_get_frustum_vertices(VkdfCamera *cam, glm::vec3 *f)
{
   vkdf_camera_get_frustum_vertices_at_distance(cam, cam->proj.far_plane, f);
}

void
vkdf_camera_get_clip_box_at_distance(VkdfCamera *cam, float dist, VkdfBox *box);

inline void
vkdf_camera_get_clip_box(VkdfCamera *cam, VkdfBox *box)
{
   return vkdf_camera_get_clip_box_at_distance(cam, cam->proj.far_plane, box);
}

#endif
