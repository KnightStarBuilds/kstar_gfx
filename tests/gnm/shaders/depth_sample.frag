#version 450
layout(binding=8) uniform sampler2D depth_image;
layout(location=0) out vec4 color;
void main() { float z=textureLod(depth_image,vec2(35.5/128.0,64.5/128.0),0).r; color=vec4(z,z,z,1); }
