// [DynamicShadows] caster: the captured world meshes are re-drawn from the sun into the shadow map with this shader.
// attribute 0 = aPos (the mesh's Position), attribute 1 = aUV (TexCoord0). uObj = the mesh's world matrix, uLightVP = the sun matrix.
attribute vec3 aPos; attribute vec4 aUV;
uniform mat4 uLightVP; uniform mat4 uObj; uniform float uUvScale;
varying vec2 vUV;
void main(){ vUV = aUV.xy * uUvScale; gl_Position = uLightVP * (uObj * vec4(aPos, 1.0)); }
