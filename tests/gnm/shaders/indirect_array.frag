#version 450
// Runtime polygon length and modulo access reproduce LTC clipping's local
// array traffic. Standalone PSBC must lower this before ACO selection.
layout(binding=0) uniform Inputs {vec4 values[4];ivec4 control;};
layout(location=0) out vec4 color;
void main(){
    vec3 polygon[8];int count=0;
    for(int i=0;i<4;++i){if(i<control.y&&values[i].z>0)polygon[count++]=values[i].xyz;}
    vec3 sum=vec3(0);
    for(int i=0;i<8;++i){if(i>=count)break;sum+=polygon[(i+control.x)%count];}
    color=vec4(sum,1);
}
