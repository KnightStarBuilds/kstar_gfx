#version 450
layout(location=0) out vec4 color;
void main() {
    // Exercise Mesa's signed-front-face optimization, not just a boolean branch.
    float facing = 1.0;
    if (!gl_FrontFacing) facing = -facing;
    color = vec4(0.5 + 0.5 * facing, 0.0, 0.5 - 0.5 * facing, 1.0);
}
