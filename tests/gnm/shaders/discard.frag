#version 450
layout(location=0) in vec4 color;
void main() { if(color.a<0.3) discard; }
