#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 2, binding = 0) uniform sampler2D tex_opacity;

layout(location = 0) in vec2 in_uv;

void main()
{
   /* Always sample opacity from LOD 0: mipmaps can have non 1.0 values for
    * visible pixels as a result of filtering.
    */
   float opacity = textureLod(tex_opacity, in_uv, 0).r;
   if (opacity < 0.8) {
      discard;
   }
}
