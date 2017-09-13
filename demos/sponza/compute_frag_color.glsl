   // Fix light position to be in tangent space like all our other
   // lighting variables
   Light light = L.sun;
   light.pos = in_tangent_light_pos;

   // Get material for this fragment
   Material mat = Mat.materials[in_material_idx];

   // Take diffuse component from texture if needed
   if (mat.diffuse_tex_count > 0)
      mat.diffuse = texture(tex_diffuse, in_uv);

   // Ambient material (we just use the diffuse aspect for ambient)
   mat.ambient = mat.diffuse;

   // Normal mapping
   vec3 tangent_normal;
   if (mat.normal_tex_count > 0)
      tangent_normal = normalize(texture(tex_normal, in_uv).rgb * 2.0 - 1.0);
   else
      tangent_normal = in_tangent_normal;

   // Take specular component from texture if needed
   if (mat.specular_tex_count > 0)
      mat.specular = texture(tex_specular, in_uv);

   // Do lighting computations (all done in tangent space)
   LightColor color =
      compute_lighting(light,
                       in_tangent_pos.xyz,
                       tangent_normal,
                       in_tangent_view_dir,
                       mat,
                       true,
                       in_light_space_pos,
                       shadow_map,
                       SMD.shadow_map_data.shadow_map_size,
                       SMD.shadow_map_data.pfc_kernel_size);

   out_color = vec4(color.diffuse + color.ambient + color.specular, 1);
//   out_color = vec4(in_debug, 1);
