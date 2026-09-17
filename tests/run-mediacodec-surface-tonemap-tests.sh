#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out_dir="${repo_root}/tests/data/mediacodec-surface-tonemap"
extract_c="${out_dir}/extract_shader.c"
shader_glsl="${out_dir}/fragment_shader.glsl"
shader_compiler="${out_dir}/shader_compile"
vectors="${out_dir}/vectors"

mkdir -p "$out_dir"

awk '
    /static const char \*fragment_shader =/ {
        print "#include <stdio.h>"
        print "int main(void) {"
        sub(/^.*= /, "    const char *fragment_shader = ")
        print
        in_shader = 1
        next
    }
    in_shader {
        print
        if ($0 ~ /";$/) {
            print "    fputs(fragment_shader, stdout);"
            print "    return 0;"
            print "}"
            exit
        }
    }
' "${repo_root}/libavcodec/mediacodec_gl.c" > "$extract_c"

cc -Wall -Wextra -Werror "$extract_c" -o "${out_dir}/extract_shader"
"${out_dir}/extract_shader" > "$shader_glsl"

cc -Wall -Wextra -Werror \
    "${repo_root}/tests/mediacodec_surface_tonemap_shader_compile.c" \
    -o "$shader_compiler" $(pkg-config --cflags --libs egl glesv2)

mesa_vendor=/usr/share/glvnd/egl_vendor.d/50_mesa.json
if [ -f "$mesa_vendor" ]; then
    __EGL_VENDOR_LIBRARY_FILENAMES="$mesa_vendor" \
    EGL_PLATFORM=surfaceless \
    LIBGL_ALWAYS_SOFTWARE=1 \
    MESA_LOADER_DRIVER_OVERRIDE=llvmpipe \
    JFAT_REQUIRE_LLVMPIPE=1 \
    "$shader_compiler" "$shader_glsl"
else
    EGL_PLATFORM=surfaceless "$shader_compiler" "$shader_glsl"
fi

cc -Wall -Wextra -Werror \
    "${repo_root}/tests/mediacodec_surface_tonemap_vectors.c" \
    -o "$vectors" -lm
"$vectors"

printf '%s\n' "MediaCodec surface tonemap tests passed"
