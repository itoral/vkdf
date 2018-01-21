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

   glm::vec3 viewdir;
   glm::mat4 view_matrix;
   glm::mat4 rot_matrix;

   VkdfFrustum frustum;

   /* If you add new flags here, check if they need to be cleared in
    * vkdf_camera_set_dirty()
    */
   bool dirty;
   bool dirty_position;
   bool dirty_viewdir;
   bool dirty_view_matrix;
   bool dirty_rot_matrix;
   bool dirty_frustum;
} VkdfCamera;

VkdfCamera *
vkdf_camera_new(float px, float py, float pz,
                float rx, float ry, float rz,
                float fov, float near, float far, float aspect_ratio);

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

inline bool
vkdf_camera_has_dirty_position(VkdfCamera *cam)
{
   assert(!cam->dirty_position || cam->dirty);
   return cam->dirty_position;
}

inline void
vkdf_camera_set_dirty(VkdfCamera *cam, bool dirty)
{
   cam->dirty = dirty;
   if (!dirty) {
      /* Most of our dirty states relate to cached data, in the sense that when
       * they are False it means that we can reused cached data. We use them to
       * save us from doing redundant expensive computations. When the user
       * signals that the camera is no longer dirty, it means that it is done
       * using it in that frame, but that doesn't invalidate any cached data,
       * so here we only clear flags that are not used for caching purposes.
       */
      cam->dirty_position = false;
   }
}

glm::mat4
vkdf_camera_get_view_matrix(VkdfCamera *cam);

glm::mat4
vkdf_camera_get_rotation_matrix(VkdfCamera *cam);

const glm::vec3 *
vkdf_camera_get_frustum_vertices(VkdfCamera *cam);

const VkdfBox *
vkdf_camera_get_frustum_box(VkdfCamera *cam);

const VkdfPlane *
vkdf_camera_get_frustum_planes(VkdfCamera *cam);

#endif
