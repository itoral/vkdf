#ifndef __VKDF_OBJECT_H__
#define __VKDF_OBJECT_H__

typedef struct {
   glm::vec3 pos;
   glm::vec3 rot;
   glm::vec3 scale;

   VkdfModel *model;
   VkdfBox box;

   // In theory each mesh in a model has at most 1 material. However, it is
   // useful to add variants of the materials, for example, to have different
   // color versions of the same model. For that, we can use this field to
   // offset the mesh materials and select the variant we want for this
   // particular object. A value of 0 would select the default material,
   // if any, 1 would select the first variant, etc
   uint32_t material_idx_base;

   bool is_dynamic;
} VkdfObject;

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
   obj->pos = pos;
}

inline void
vkdf_object_set_rotation(VkdfObject *obj, const glm::vec3 &rot)
{
   obj->rot = rot;
}

inline void
vkdf_object_set_scale(VkdfObject *obj, const glm::vec3 &scale)
{
   obj->scale = scale;
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
   return obj->model->size.w * obj->scale.x;
}

inline float
vkdf_object_height(VkdfObject *obj)
{
   return obj->model->size.h * obj->scale.y;
}

inline float
vkdf_object_depth(VkdfObject *obj)
{
   return obj->model->size.d * obj->scale.z;
}

void
vkdf_object_compute_box(VkdfObject *obj);

inline VkdfBox *
vkdf_object_get_box(VkdfObject *obj)
{
   return &obj->box;
}

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

#endif
