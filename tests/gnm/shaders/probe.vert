#version 450
layout(location=0) out vec3 color;
void main(){vec2 position=gl_VertexIndex==0?vec2(-.8,-.8):gl_VertexIndex==1?vec2(.8,-.8):vec2(0,.8);gl_Position=vec4(position,.5,1);color=vec3(1,0,0);}
