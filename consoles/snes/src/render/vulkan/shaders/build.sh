#!/bin/sh
# Compile the SNES's shaders to SPIR-V and write them into ../rr_spv_snes.h,
# which is checked in: building the game needs no shader compiler. Run after
# editing m7.frag, m7_gl.glsl, m7_body.glsl, compose.comp, compose_rec.comp
# or ../../compose_body.h (glslangValidator, from glslang or the Vulkan
# SDK). The kit's own shaders (quad.vert, copy.frag, sharp.frag, over.frag)
# are kit/src/render/vulkan/shaders/build.sh's. The Mode 7 shader's body is
# the OpenGL backend's too: it is written, after m7_gl.glsl's prelude, into
# ../../gl/rr_m7_glsl.h, checked against the compiler as OpenGL GLSL first.
set -e
cd "$(dirname "$0")"
glslangValidator -V --vn k_m7_frag -o m7.h m7.frag >/dev/null
glslangValidator -V --vn k_compose_comp -o compose.h compose.comp >/dev/null
glslangValidator -V --vn k_compose_rec_comp -o compose_rec.h compose_rec.comp >/dev/null
{
  echo "/* rr_spv_snes.h: SPIR-V of shaders/m7.frag, compose.comp and compose_rec.comp,"
  echo " * written by shaders/build.sh: do not edit. */"
  for f in m7.h compose.h compose_rec.h; do sed -n '/^const uint32_t/,$p' $f | sed 's/^const/static const/'; done
} > ../rr_spv_snes.h
rm -f m7.h compose.h compose_rec.h
cat m7_gl.glsl m7_body.glsl > m7_gl.frag
glslangValidator m7_gl.frag >/dev/null
{
  echo "/* rr_m7_glsl.h: the OpenGL backend's Mode 7 fragment shader, shaders/m7_gl.glsl"
  echo " * and m7_body.glsl, written by vulkan/shaders/build.sh: do not edit. */"
  echo "static const char *k_m7 ="
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/' m7_gl.frag
  echo "    ;"
} > ../../gl/rr_m7_glsl.h
rm -f m7_gl.frag
echo "wrote rr_spv_snes.h, ../../gl/rr_m7_glsl.h"
