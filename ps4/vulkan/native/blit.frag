#version 450
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(std140,set=0,binding=1) uniform BlitCoordinates { vec4 scaleBias; } coordinates;
layout(location=0) out vec4 color;
void main() {
    color=textureLod(sourceImage,gl_FragCoord.xy*coordinates.scaleBias.xy+coordinates.scaleBias.zw,0.0);
}
