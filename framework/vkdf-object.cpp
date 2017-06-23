#include "vkdf.hpp"

inline static void
init_object(VkdfObject *obj, const glm::vec3 &pos)
{
   obj->pos = pos;
   obj->rot = glm::vec3(0.0f, 0.0f, 0.0f);
   obj->scale = glm::vec3(1.0f, 1.0f, 1.0f);
}

VkdfObject *
vkdf_object_new_from_mesh(const glm::vec3 &pos, VkdfMesh *mesh)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = vkdf_model_new();
   vkdf_model_add_mesh(obj->model, mesh);

   return obj;
}

VkdfObject *
vkdf_object_new_from_model(const glm::vec3 &pos,
                           VkdfModel *model)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = model;

   return obj;
}

VkdfObject *
vkdf_object_new(const glm::vec3 &pos, VkdfModel *model)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = model;

   return obj;
}

void
vkdf_object_free(VkdfObject *obj)
{
   // Models are not owned by the objects
   g_free(obj);
}

glm::mat4
vkdf_object_get_model_matrix(VkdfObject *obj)
{
   glm::mat4 model = glm::translate(glm::mat4(1.0f), obj->pos);

   if (obj->rot.x != 0.0f || obj->rot.y != 0.0f || obj->rot.z != 0.0f) {
      glm::vec3 rot = glm::vec3(DEG_TO_RAD(obj->rot.x),
                                DEG_TO_RAD(obj->rot.y),
                                DEG_TO_RAD(obj->rot.z));
      glm::tquat<float> quat = glm::quat(rot);
      glm::mat4 rot_matrix = glm::toMat4(quat);
      model = model * rot_matrix;
   }

   if (obj->scale.x != 1.0f || obj->scale.y != 1.0f || obj->scale.z != 1.0f)
      model = glm::scale(model, obj->scale);

   return model;
}

