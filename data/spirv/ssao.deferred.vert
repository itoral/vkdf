#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   mat4 Proj;
   vec2 noise_scale;
   int num_samples;
   float radius;
   float bias;
   float intensity;
   float aspect_ratio;
   float tan_half_fov;
} PCB;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_view_ray;

void main()
{
   switch (gl_VertexIndex) {
   case 0:
      gl_Position = vec4( 1.0,  1.0, 0.0, 1.0);
      out_uv = vec2(1.0, 1.0);
      break;
   case 1:
      gl_Position = vec4( 1.0, -1.0, 0.0, 1.0);
      out_uv = vec2(1.0, 0.0);
      break;
   case 2:
      gl_Position = vec4(-1.0,  1.0, 0.0, 1.0);
      out_uv = vec2(0.0, 1.0);
      break;
   case 3:
      gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
      out_uv = vec2(0.0, 0.0);
      break;
   }

   /* This will be used in the FS to construct eye-space fragment coordinates
    * for each fragment in the scene from their Z values in the depth buffer.
    *
    * Notice that the X component is negated (unlike OpenGL). This is a
    * consequence of Vulkan's coordinate system differences.
    */
   out_view_ray.x = -gl_Position.x * PCB.aspect_ratio * PCB.tan_half_fov;
   out_view_ray.y = gl_Position.y * PCB.tan_half_fov;
}
