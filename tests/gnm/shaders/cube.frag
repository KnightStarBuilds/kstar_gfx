#version 450
layout(binding=8) uniform samplerCube source_image;
layout(location=0) out vec4 output_color;
void main() { output_color=textureLod(source_image,vec3(1,0,0),1); }
