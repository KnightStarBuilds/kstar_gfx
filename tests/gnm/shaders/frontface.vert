#version 450
void main() {
    int index=gl_VertexIndex;
    if(gl_InstanceIndex==1 && index>0) index=3-index;
    vec2 p=index==0?vec2(-.8,-.8):index==1?vec2(.8,-.8):vec2(0,.8);
    gl_Position=vec4(p*vec2(.4,.8)+vec2(gl_InstanceIndex==0?-.45:.45,0),0,1);
}
