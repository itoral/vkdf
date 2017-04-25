#ifndef __VKDF_OBJECT_H__
#define __VKDF_OBJECT_H__

typedef struct {
   glm::vec3 pos;
   glm::vec3 rot;
   glm::vec3 scale;

   VkdfModel *model;

   // In theory each mesh in a model has at most 1 material. However, it is
   // useful to add variants of the materials, for example, to have different
   // color versions of the same model. For that, we can use this field to
   // offset the mesh materials and select the variant we want for this
   // particular object. A value of 0 would select the default material,
   // if any, 1 would select the first variant, etc
   uint32_t material_idx_base;
} VkdfObject;

VkdfObject *
vkdf_object_new_from_mesh(const glm::vec3 &pos, VkdfMesh *mesh);

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

glm::mat4
vkdf_object_get_model_matrix(VkdfObject *obj);

#endif
