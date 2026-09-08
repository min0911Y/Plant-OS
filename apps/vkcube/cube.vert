#version 450
layout(push_constant) uniform Rotation { vec4 rotation; float aspect; } pc;
layout(location = 0) out vec3 color;
const vec3 vertices[8] = vec3[](
  vec3(-1,-1,-1), vec3(1,-1,-1), vec3(1,1,-1), vec3(-1,1,-1),
  vec3(-1,-1,1), vec3(1,-1,1), vec3(1,1,1), vec3(-1,1,1));
const int indices[36] = int[](
  0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,4,7, 0,7,3,
  1,2,6, 1,6,5, 3,7,6, 3,6,2, 0,1,5, 0,5,4);
const vec3 colors[6] = vec3[](
  vec3(0.9,0.2,0.15), vec3(0.15,0.75,0.95), vec3(0.2,0.85,0.35),
  vec3(0.95,0.65,0.15), vec3(0.65,0.3,0.9), vec3(0.9,0.35,0.6));
void main() {
  vec3 p = vertices[indices[gl_VertexIndex]];
  p.xz = mat2(pc.rotation.y, -pc.rotation.x,
              pc.rotation.x, pc.rotation.y) * p.xz;
  p.yz = mat2(pc.rotation.w, -pc.rotation.z,
              pc.rotation.z, pc.rotation.w) * p.yz;
  float z = p.z + 4.5;
  gl_Position = vec4(p.x * 2.0 / pc.aspect, p.y * 2.0,
                     (10.0 * z - 10.0) / 9.0, z);
  color = colors[gl_VertexIndex / 6];
}
