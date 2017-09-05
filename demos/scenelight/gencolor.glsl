   // FIXME: instead of using two sampler bindings we should use an array of
   // samplers to simplify this. Unfortunately, Mesa fails to compile such
   // shader at present.
   out_color = vec4(0.0, 0.0, 0.0, 1.0);

   LightColor color;
   color = compute_lighting(L.lights[0],
                            in_world_pos.xyz,
                            in_normal, in_view_dir,
                            mat,
                            bool(in_receives_shadows),
                            in_light_space_pos[0],
                            shadow_map0,
                            SMD.data[0].shadow_map_size,
                            SMD.data[0].pfc_kernel_size);

   out_color.xyz += color.diffuse + color.ambient + color.specular;

   color = compute_lighting(L.lights[1],
                            in_world_pos.xyz,
                            in_normal, in_view_dir,
                            mat,
                            bool(in_receives_shadows),
                            in_light_space_pos[1],
                            shadow_map1,
                            SMD.data[1].shadow_map_size,
                            SMD.data[1].pfc_kernel_size);

   out_color.xyz += color.diffuse + color.ambient + color.specular;
