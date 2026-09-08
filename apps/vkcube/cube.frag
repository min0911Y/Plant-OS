#version 450
layout(location = 0) in vec3 color;
layout(location = 0) out vec4 output_color;
void main() { output_color = vec4(color, 1.0); }
