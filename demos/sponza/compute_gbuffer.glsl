   // Eye-space viewdir, lightpos, light-space pos
   //
   // We skip eye-space position to save bandwidth. Shaders that need
   // this will have to reconstruct it from depth.
   out_eye_light_pos = in_eye_light_pos;
   out_light_space_pos = in_light_space_pos;

   Material mat = Mat.materials[in_material_idx];

   // Eye-space normal
   if (mat.normal_tex_count > 0) {
      mat3 TBN = mat3(in_eye_tangent, in_eye_bitangent, in_eye_normal);
      vec3 tangent_normal = texture(tex_normal, in_uv).rgb * 2.0 - 1.0;
      out_eye_normal = vec4(normalize(TBN * tangent_normal), 0);
   } else {
      out_eye_normal = vec4(in_eye_normal, 0);
   }

   // Diffuse
   if (mat.diffuse_tex_count > 0)
      mat.diffuse = texture(tex_diffuse, in_uv);
   out_diffuse = mat.diffuse;

   // Specular
   if (mat.specular_tex_count > 0)
      mat.specular = texture(tex_specular, in_uv);
   out_specular = mat.specular;

   // Shininess
   out_eye_normal.w = mat.shininess;
