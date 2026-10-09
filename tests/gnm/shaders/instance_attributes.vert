#version 450
layout(location=0) in vec3 position;
layout(location=3) in vec2 instance_offset;
layout(location=4) in vec4 instance_color;
layout(location=0) out vec4 color;
void main() {
    gl_Position=vec4(position.xy*vec2(.4,.8)+instance_offset,0,1);
    color=instance_color;
}
