#version 450
layout(binding=8) uniform sampler2DMS source_image;
layout(location=0) out vec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    color = (texelFetch(source_image,p,0) + texelFetch(source_image,p,1)
           + texelFetch(source_image,p,2) + texelFetch(source_image,p,3)) * 0.25;
}
