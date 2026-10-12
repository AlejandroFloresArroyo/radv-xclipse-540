#version 450
// Stage 3 vertex shader: one triangle from gl_VertexIndex, no vertex buffers.
const vec2 pos[3] = vec2[](vec2(0.0, -0.75), vec2(0.75, 0.75), vec2(-0.75, 0.75));

void main()
{
   gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
}
