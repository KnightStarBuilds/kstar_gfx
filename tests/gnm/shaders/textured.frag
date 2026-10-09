#version 450
layout(binding=0,std140) uniform Params { vec4 tint; };
layout(binding=8) uniform sampler2D image0;
layout(location=0) out vec4 result;
void main(){result=texture(image0,vec2(.25))*tint;}
