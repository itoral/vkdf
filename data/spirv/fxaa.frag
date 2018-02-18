#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform pcb {
   /* Minimum luma in the pixel neighbourhood to do antialiasing
    *
    * If none of the pixels in the neighbourhood reach this minimum, then
    * antialising is skipped for this pixel. Increasing this makes antialising
    * ignore darker areas, increasing performance by discarding more pixels.
    */
   float luma_min;

   /* Minimum luma range in a pixel's neighbourhood required to do antialiasing
    *
    * Increasing this makes antialiasing focus on highest contrast areas only
    * improving performance by discardig more pixels.
    */
   float luma_range_min;

   /* Subpixel antialiasing
    *
    * This controls antialising of "single dot pixels". Increasing its value
    * will make for a stronger antialiasing of single-pixel dots in the image,
    * but can also lead to moreoverall blurriness.
    */
   float subpx_aa;
} PCB;

/* Number of iterations for edge border detection, and speed of each iteration
 *
 * Increasing this can make the algorithm  accurate at the expense of
 * performance. Increasing speed in each iteration can improve performance at
 * the expense of accuracy. This mostly comes into play when we have long edges.
 */
const int   MAX_ITERATIONS        = 12;
const float SPEED[MAX_ITERATIONS] =
   { 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 4.0, 6.0, 6.0, 8.0, 8.0, 8.0 };

layout(set = 0, binding = 0) uniform sampler2D tex_source;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

/* Computes pixel luma.
 *
 * Expects a linear RGB color as input and computes the luma in sRGB space,
 * since we want to do antialiasing in the color space in which the user
 * will see the final image.
 */
float rgb2luma(vec3 rgb)
{
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main()
{
   /* Compute this fragment's luma */
   vec3 colorC = texture(tex_source, in_uv).rgb;
   float lumaC = rgb2luma(colorC);

   /* Compute luma at the neighbouring fragments */
   float lumaD = rgb2luma(textureOffset(tex_source, in_uv, ivec2( 0,-1)).rgb);
   float lumaU = rgb2luma(textureOffset(tex_source, in_uv, ivec2( 0, 1)).rgb);
   float lumaL = rgb2luma(textureOffset(tex_source, in_uv, ivec2(-1, 0)).rgb);
   float lumaR = rgb2luma(textureOffset(tex_source, in_uv, ivec2( 1, 0)).rgb);

   /* If the maximum luma is too dark, skip antialiasing */
   float max_luma = max(lumaC, max(max(lumaD, lumaU), max(lumaL, lumaR)));
   if (max_luma < PCB.luma_min) {
       out_color = vec4(colorC, 1.0);
       return;
   }

   /* Compute luma range in the neighbourhood */
   float min_luma = min(lumaC, min(min(lumaD, lumaU), min(lumaL, lumaR)));
   float luma_range = max_luma - min_luma;

   /* If the luma range is too small then there is no aliasing */
   if (luma_range < PCB.luma_range_min) {
       out_color = vec4(colorC, 1.0);
       return;
   }

   /* Estimate gradient along horizontal and vertical directions to decide
    * the direction of the edge
    */
   float lumaDL = rgb2luma(textureOffset(tex_source, in_uv, ivec2(-1,-1)).rgb);
   float lumaUR = rgb2luma(textureOffset(tex_source, in_uv, ivec2( 1, 1)).rgb);
   float lumaUL = rgb2luma(textureOffset(tex_source, in_uv, ivec2(-1, 1)).rgb);
   float lumaDR = rgb2luma(textureOffset(tex_source, in_uv, ivec2( 1,-1)).rgb);

   float lumaDU   = lumaD + lumaU;
   float lumaLR   = lumaL + lumaR;
   float lumaDLUL = lumaDL + lumaUL;
   float lumaDLDR = lumaDL + lumaDR;
   float lumaDRUR = lumaDR + lumaUR;
   float lumaURUL = lumaUR + lumaUL;

   float gradH = abs(-2.0 * lumaL + lumaDLUL) +
                 abs(-2.0 * lumaC + lumaDU ) * 2.0 +
                 abs(-2.0 * lumaR + lumaDRUR);

   float gradV = abs(-2.0 * lumaU + lumaURUL) +
                 abs(-2.0 * lumaC + lumaLR) * 2.0 +
                 abs(-2.0 * lumaD + lumaDLDR);

   bool is_horizontal = gradH >= gradV;

   /* Find the edge border in the direction that is orthogonal to the direction
    * of the edge. The edge border will be 0.5 texels in the orientation
    * (positive or negative) where we find the largest luma gradient.
    */
   float luma_neg = is_horizontal ? lumaD : lumaL;
   float luma_pos = is_horizontal ? lumaU : lumaR;

   float grad_neg = abs(luma_neg - lumaC);
   float grad_pos = abs(luma_pos - lumaC);

   vec2 texel_size = 1.0 / textureSize(tex_source, 0);
   float step_length = is_horizontal ? texel_size.y : texel_size.x;

   float luma_local_avg;
   if (grad_neg >= grad_pos) {
       step_length = -step_length;
       luma_local_avg = 0.5 * (luma_neg + lumaC);
   } else {
       luma_local_avg = 0.5 * (luma_pos + lumaC);
   }

   vec2 edge_uv = in_uv;
   if (is_horizontal) {
       edge_uv.y += step_length * 0.5;
   } else {
       edge_uv.x += step_length * 0.5;
   }

   /* Step in the direction of the border until we find its end. We do this
    * computing texels along each orientation (so on each side of the edge)
    * and comparing their lumas against the local luma average. If the luma
    * for one of these texels is larger than the local average it means we
    * reached that end of the border.
    */
   float grad_scaled = 0.25 * max(abs(grad_neg), abs(grad_pos));
   vec2 offset = is_horizontal ? vec2(texel_size.x, 0.0) :
                                 vec2(0.0, texel_size.y);
   bool end_neg_found = false;
   bool end_pos_found = false;
   vec2 uv_pos = edge_uv;
   vec2 uv_neg = edge_uv;
   for (int i = 0; i < MAX_ITERATIONS; i++) {
      if (!end_neg_found) {
         uv_neg -= offset * SPEED[i];
         luma_neg = rgb2luma(texture(tex_source, uv_neg).rgb) - luma_local_avg;
      }

      if (!end_pos_found) {
         uv_pos += offset * SPEED[i];
         luma_pos = rgb2luma(texture(tex_source, uv_pos).rgb) - luma_local_avg;
      }

      end_neg_found = abs(luma_neg) >= grad_scaled;
      end_pos_found = abs(luma_pos) >= grad_scaled;

      if (end_neg_found && end_pos_found)
         break;
   };

   /* Find the closest border end and find how far the current pixel is from it.
    * Use that information to compute a UV offset between [-0.5, 0.5] where
    * +/-0.5 means that we are on a border end and 0 means that we are right in
    * the middle.
    */
   float dist_neg = is_horizontal ? (in_uv.x - uv_neg.x) : (in_uv.y - uv_neg.y);
   float dist_pos = is_horizontal ? (uv_pos.x - in_uv.x) : (uv_pos.y - in_uv.y);

   float dist = min(dist_neg, dist_pos);
   bool neg_is_closer = dist_neg <  dist_pos;
   float edge_length = dist_neg + dist_pos;

   float px_offset = -dist / edge_length + 0.5;

   /* Make sure that the border end we found is consistent with the aliasing
    * that we were trying to address for this pixel, otherwise it means that
    * we stepped too far away from the border end, in which case the offset
    * is not good.
    */
   if (((neg_is_closer ? luma_neg : luma_pos) < 0.0) == (lumaC < luma_local_avg))
      px_offset = 0.0;

   /* Subpixel antialiasing
    *
    * This takes care of antialing / blurring single pixels (dots) in the
    * image. For that it computes the difference between the average luma in
    * the 3x3 neighbourhood with the luma at the pixel (in the center). A large
    * difference identifies a single pixel aliasing.
    */
   float luma_avg = (1.0 / 10.0) * (1.5 * (lumaDU + lumaLR) + lumaDLUL + lumaDRUR);
   float subpx_offset1 = abs(luma_avg - lumaC) / luma_range;
   float subpx_offset2 = (-2.0 * subpx_offset1 + 3.0) * subpx_offset1 * subpx_offset1;
   float subpx_offset =  subpx_offset2 * subpx_offset2 * PCB.subpx_aa;

   /* Finally, we select the pixel offset to use as the largest of the two we
    * computed and we read the source image at that offset. The linear
    * filtering will take care of averaging nighbours at that position to
    * produce the antialised output for this fragment.
    */
   px_offset = max(px_offset, subpx_offset);

   vec2 uv = in_uv;
   if (is_horizontal) {
      uv.y += px_offset * step_length;
   } else {
      uv.x += px_offset * step_length;
   }

   out_color = texture(tex_source, uv);
}
