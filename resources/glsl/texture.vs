uniform mat4 modelViewMatrix;  // mesh transform
uniform mat4 projectionMatrix; // from camera

attribute vec3 position;
attribute vec2 texcoord0;

varying vec2 vtexcoord0;

void main(void)
{
  vec4 pos = vec4(position, 1.0);
  vtexcoord0 = texcoord0;
  gl_Position = projectionMatrix*modelViewMatrix*pos; // equivalent to builtin function ftransform()
}
