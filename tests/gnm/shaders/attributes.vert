#version 450
layout(location=0) in vec3 position;
layout(location=3) in vec4 vertex_color;
layout(binding=0) uniform Params { vec4 tint; vec4 control; } params;
layout(location=0) out vec4 color;
void main() {
    gl_Position=vec4(position.x*.4+(gl_InstanceIndex==0?-.45:.45),position.y*.8,params.control.x*2.0-1.0,1);
    color=vertex_color*params.tint;
}
