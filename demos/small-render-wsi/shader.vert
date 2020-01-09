#version 400

void main()
{
   vec4 pos;
   switch (gl_VertexIndex) {
   case 0:
      pos = vec4( 0.0, 0.5, 0.0, 1.0);
      break;
   case 1:
      pos = vec4(-0.5,-0.5, 0.0, 1.0);
      break;
   case 2:
      pos = vec4(0.5, -0.5, 0.0, 1.0);
      break;
   }
   gl_Position = vec4(pos.x, -pos.y, pos.z, pos.w);
}
