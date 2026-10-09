#version 450
layout(binding=0) uniform Copy { ivec4 rectangle; } copy_region;
layout(binding=8) uniform sampler2D source_image;
layout(location=0) out vec4 color;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy)+copy_region.rectangle.xy-copy_region.rectangle.zw;
    color=texelFetch(source_image,p,0);
}
