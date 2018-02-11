#ifndef __VKDF_CAMERA_H__
#define __VKDF_CAMERA_H__

enum {
   VKDF_CAMERA_DIRTY             = (1 << 0),
   VKDF_CAMERA_DIRTY_POS         = (1 << 1),
   VKDF_CAMERA_DIRTY_VIEW_DIR    = (1 << 2),
   VKDF_CAMERA_DIRTY_VIEW_MAT    = (1 << 3),
   VKDF_CAMERA_DIRTY_ROT_MAT     = (1 << 4),
   VKDF_CAMERA_DIRTY_FRUSTUM     = (1 << 5),
};

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

   uint32_t dirty;
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

glm::vec3
vkdf_camera_get_viewdir(VkdfCamera *cam);

void
vkdf_camera_look_at(VkdfCamera *cam, float x, float y, float z);

inline bool
vkdf_camera_is_dirty(VkdfCamera *cam)
{
   return (bool) cam->dirty;
}

inline bool
vkdf_camera_has_dirty_position(VkdfCamera *cam)
{
   assert(!bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY_POS) ||
          bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY));

   return bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY_POS);
}

inline bool
vkdf_camera_has_dirty_viewdir(VkdfCamera *cam)
{
   assert(!bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY_VIEW_DIR) ||
          bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY));

   return bitfield_get(cam->dirty, VKDF_CAMERA_DIRTY_VIEW_DIR);
}

inline void
vkdf_camera_set_dirty(VkdfCamera *cam, bool dirty)
{
   if (dirty)
      bitfield_set(&cam->dirty, VKDF_CAMERA_DIRTY);
   else
      bitfield_unset(&cam->dirty, VKDF_CAMERA_DIRTY);

   if (!dirty) {
      /* Most of our dirty states relate to cached data, in the sense that when
       * they are False it means that we can reuse cached data. We use them to
       * save us from doing redundant expensive computations. When the user
       * signals that the camera is no longer dirty, it means that it is done
       * using it in that frame, but that doesn't invalidate any cached data,
       * so here we only clear flags that are not used for caching purposes.
       */
      bitfield_unset(&cam->dirty, VKDF_CAMERA_DIRTY_POS);
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
