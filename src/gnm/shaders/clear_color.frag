#version 450
layout(binding=0) uniform Clear { vec4 value; } clear;
layout(location=0) out vec4 color;
void main() { color = clear.value; }
