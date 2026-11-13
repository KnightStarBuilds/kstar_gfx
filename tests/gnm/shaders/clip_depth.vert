#version 450
layout(location=0) in vec4 clip_position;
layout(location=3) in vec4 vertex_color;
layout(location=0) out vec4 color;
void main() {
    gl_Position=clip_position;
    color=vertex_color;
}
