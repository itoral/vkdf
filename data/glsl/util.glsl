/**
 * Computes eye-space Z for the pixel's depth taken from the depth buffer
 * at the given texture-space coordinates given the projection matrix
 * provided.
 *
 * Notice that the formula here is Vulkan-specific due to the differences
 * between OpenGL and Vulkan coordinate systems.
 */
float
compute_eye_z_from_depth(sampler2D tex_depth, vec2 coord, mat4 Proj)
{
   float depth = texture(tex_depth, coord).x;
   return -Proj[3][2] / (Proj[2][2] + depth);
}

