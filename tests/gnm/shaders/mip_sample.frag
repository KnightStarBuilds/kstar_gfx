#version 450
layout(binding=8) uniform sampler2D source_image;
layout(location=0) out vec4 color;
void main() { color=textureLod(source_image,vec2(.25,.25),2.0); }
