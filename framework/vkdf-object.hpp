#ifndef __VKDF_OBJECT_H__
#define __VKDF_OBJECT_H__

typedef struct {
   glm::vec3 pos;
   glm::vec3 rot;
   glm::vec3 scale;

   VkdfModel *model;

   VkdfBox box;
   VkdfBox *mesh_boxes;

   glm::mat4 model_matrix;

   // In theory each mesh in a model has at most 1 material. However, it is
   // useful to add variants of the materials, for example, to have different
   // color versions of the same model. For that, we can use this field to
   // offset the mesh materials and select the variant we want for this
   // particular object. A value of 0 would select the default material,
   // if any, 1 would select the first variant, etc
   uint32_t material_idx_base;

   bool is_dynamic;

   bool dirty;
   bool dirty_model_matrix;
   bool dirty_box;
   bool dirty_mesh_boxes;

   bool receives_shadows;
   bool casts_shadows;
} VkdfObject;

#define SET_FIELD(obj, field, value)   \
{                                      \
   field = value;                      \
   obj->dirty = true;                  \
   obj->dirty_model_matrix = true;     \
   obj->dirty_box = true;              \
   obj->dirty_mesh_boxes = true;       \
}

VkdfObject *
vkdf_object_new_from_mesh(const glm::vec3 &pos, VkdfMesh *mesh);

VkdfObject *
vkdf_object_new_from_model(const glm::vec3 &pos, VkdfModel *model);

VkdfObject *
vkdf_object_new(const glm::vec3 &pos, VkdfModel *model);

void
vkdf_object_free(VkdfObject *obj);

inline void
vkdf_object_set_position(VkdfObject *obj, const glm::vec3 &pos)
{
   SET_FIELD(obj, obj->pos, pos)
}

inline void
vkdf_object_set_rotation(VkdfObject *obj, const glm::vec3 &rot)
{
   SET_FIELD(obj, obj->rot, rot)
}

inline void
vkdf_object_set_scale(VkdfObject *obj, const glm::vec3 &scale)
{
   SET_FIELD(obj, obj->scale, scale)
}

inline void
vkdf_object_set_material_idx_base(VkdfObject *obj, uint32_t material_idx_base)
{
   obj->material_idx_base = material_idx_base;
}

inline uint32_t
vkdf_object_get_material_idx_base(VkdfObject *obj)
{
   return obj->material_idx_base;
}

glm::mat4
vkdf_object_get_model_matrix(VkdfObject *obj);

inline float
vkdf_object_width(VkdfObject *obj)
{
   return 2.0f * obj->model->box.w * obj->scale.x;
}

inline float
vkdf_object_height(VkdfObject *obj)
{
   return 2.0f * obj->model->box.h * obj->scale.y;
}

inline float
vkdf_object_depth(VkdfObject *obj)
{
   return 2.0f * obj->model->box.d * obj->scale.z;
}

VkdfBox *
vkdf_object_get_box(VkdfObject *obj);

const VkdfBox *
vkdf_object_get_mesh_boxes(VkdfObject *obj);

inline void
vkdf_object_set_dynamic(VkdfObject *obj, bool dynamic)
{
   obj->is_dynamic = dynamic;
}

inline bool
vkdf_object_is_dynamic(VkdfObject *obj)
{
   return obj->is_dynamic;
}

inline void
vkdf_object_set_lighting_behavior(VkdfObject *obj,
                                  bool casts_shadows,
                                  bool receives_shadows)
{
   obj->casts_shadows = casts_shadows;
   obj->receives_shadows = receives_shadows;
}

inline bool
vkdf_object_casts_shadows(VkdfObject *obj)
{
   return obj->casts_shadows;
}

inline bool
vkdf_object_receives_shadows(VkdfObject *obj)
{
   return obj->receives_shadows;
}

inline void
vkdf_object_set_dirty(VkdfObject *obj, bool dirty)
{
   assert(dirty || (!obj->dirty_model_matrix && !obj->dirty_box));
   obj->dirty = dirty;
   if (dirty)
      obj->dirty_model_matrix = dirty;
   if (dirty)
      obj->dirty_box = dirty;
}

inline void
vkdf_object_set_dirty_model_matrix(VkdfObject *obj, bool dirty)
{
   obj->dirty_model_matrix = dirty;
   if (dirty)
      obj->dirty_box = dirty;
   if (dirty)
      obj->dirty = dirty;
}

inline void
vkdf_object_set_dirty_box(VkdfObject *obj, bool dirty)
{
   obj->dirty_box = dirty;
   if (dirty)
      obj->dirty_model_matrix = dirty;
   if (dirty)
      obj->dirty = dirty;
}

inline void
vkdf_object_set_dirty_mesh_boxes(VkdfObject *obj, bool dirty)
{
   /* If we mark this dirty we should've marked the main box too */
   assert(!dirty || obj->dirty_box);
   obj->dirty_mesh_boxes = dirty;
}

inline bool
vkdf_object_is_dirty(VkdfObject *obj)
{
   return obj->dirty;
}

inline bool
vkdf_object_has_dirty_model_matrix(VkdfObject *obj)
{
   return obj->dirty_model_matrix;
}

inline bool
vkdf_object_has_dirty_box(VkdfObject *obj)
{
   return obj->dirty_box;
}

bool
vkdf_object_get_visible_meshes(VkdfObject *obj,
                               const VkdfBox *frustum_box,
                               const VkdfPlane *frustum_planes,
                               bool *visible);

#undef SET_FIELD

#endif
