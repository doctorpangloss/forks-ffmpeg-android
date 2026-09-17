#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *vertex_source =
    "attribute vec4 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = position;\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *data;

    if (!f) {
        perror(path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    data = malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = 0;
    fclose(f);
    return data;
}

static char *replace_all(const char *input, const char *needle, const char *replacement)
{
    size_t input_len = strlen(input);
    size_t needle_len = strlen(needle);
    size_t replacement_len = strlen(replacement);
    size_t count = 0;
    const char *p = input;
    char *out, *w;

    while ((p = strstr(p, needle))) {
        count++;
        p += needle_len;
    }

    out = malloc(input_len + count * (replacement_len - needle_len) + 1);
    if (!out)
        return NULL;

    p = input;
    w = out;
    while (1) {
        const char *match = strstr(p, needle);
        size_t n;
        if (!match)
            break;
        n = (size_t)(match - p);
        memcpy(w, p, n);
        w += n;
        memcpy(w, replacement, replacement_len);
        w += replacement_len;
        p = match + needle_len;
    }
    strcpy(w, p);
    return out;
}

static EGLDisplay get_display(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (get_platform_display) {
#ifdef EGL_PLATFORM_SURFACELESS_MESA
        EGLDisplay display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
        if (display != EGL_NO_DISPLAY)
            return display;
#endif
    }

    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(log), &len, log);
        fprintf(stderr, "%s shader compile failed:\n%.*s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", len, log);
        exit(1);
    }

    return shader;
}

static GLuint link_program(const char *fragment_source)
{
    GLuint program = glCreateProgram();
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLint ok = 0;

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "texcoord");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei len = 0;
        glGetProgramInfoLog(program, sizeof(log), &len, log);
        fprintf(stderr, "program link failed:\n%.*s\n", len, log);
        exit(1);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

static void set_uniform_i(GLuint program, const char *name, GLint value)
{
    GLint loc = glGetUniformLocation(program, name);
    if (loc >= 0)
        glUniform1i(loc, value);
}

static void set_uniform_f(GLuint program, const char *name, GLfloat value)
{
    GLint loc = glGetUniformLocation(program, name);
    if (loc >= 0)
        glUniform1f(loc, value);
}

static void draw(GLuint program, int tonemap, unsigned char out[16])
{
    static const GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    static const unsigned char pixels[] = {
          0,   0,   0, 255,
         64,  64,  64, 255,
        128, 128, 128, 255,
        255, 255, 255, 255,
    };
    GLuint texture;

    glViewport(0, 0, 4, 1);
    glUseProgram(program);

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    set_uniform_i(program, "inputImageTexture", 0);
    set_uniform_i(program, "u_tonemap", tonemap);
    set_uniform_i(program, "u_tonemap_algorithm", 7);
    set_uniform_f(program, "u_tonemap_param", 1.0f);
    set_uniform_f(program, "u_tonemap_desat", 0.0f);
    set_uniform_f(program, "u_tonemap_peak", 100.0f);
    set_uniform_i(program, "u_tonemap_transfer", 1);
    set_uniform_i(program, "u_tonemap_matrix", 1);
    set_uniform_i(program, "u_trc", 16);
    set_uniform_i(program, "u_bt2020", 1);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);

    glDeleteTextures(1, &texture);
}

static void assert_no_tonemap_passthrough(GLuint program)
{
    unsigned char out[16];
    draw(program, 0, out);

    if (out[0] != 0 || out[4] < 63 || out[4] > 65 || out[8] < 127 || out[8] > 129 || out[12] != 255) {
        fprintf(stderr, "no-tonemap shader render mismatch: %u %u %u %u\n",
                out[0], out[4], out[8], out[12]);
        exit(1);
    }
}

static void assert_tonemap_is_monotonic(GLuint program)
{
    unsigned char out[16];
    draw(program, 1, out);

    if (!(out[0] <= out[4] && out[4] <= out[8] && out[8] <= out[12])) {
        fprintf(stderr, "tonemap shader render is not monotonic: %u %u %u %u\n",
                out[0], out[4], out[8], out[12]);
        exit(1);
    }
    if (out[12] == 0 || out[4] == 0) {
        fprintf(stderr, "tonemap shader rendered unusable black output: %u %u %u %u\n",
                out[0], out[4], out[8], out[12]);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLConfig config;
    EGLint major, minor, count;
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE,
    };
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    EGLint surface_attribs[] = {
        EGL_WIDTH, 4,
        EGL_HEIGHT, 1,
        EGL_NONE,
    };
    char *fragment_source;
    char *render_source;
    char *tmp;
    GLuint exact_program, render_program;
    const char *renderer;

    if (argc != 2) {
        fprintf(stderr, "usage: %s fragment-shader.glsl\n", argv[0]);
        return 2;
    }

    fragment_source = read_file(argv[1]);
    if (!fragment_source)
        return 2;

    display = get_display();
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "EGL is unavailable; cannot compile MediaCodec GLES shader\n");
        return 77;
    }
    if (!eglChooseConfig(display, config_attribs, &config, 1, &count) || count < 1) {
        fprintf(stderr, "No EGL ES2 config available\n");
        return 77;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "Cannot bind OpenGL ES API\n");
        return 77;
    }
    surface = eglCreatePbufferSurface(display, config, surface_attribs);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "Cannot create/make-current EGL ES2 context\n");
        return 77;
    }

    renderer = (const char *)glGetString(GL_RENDERER);
    if (renderer)
        printf("GLES renderer: %s\n", renderer);
    if (getenv("JFAT_REQUIRE_LLVMPIPE") && (!renderer || !strstr(renderer, "llvmpipe"))) {
        fprintf(stderr, "expected llvmpipe renderer, got: %s\n", renderer ? renderer : "(null)");
        return 1;
    }

    exact_program = link_program(fragment_source);
    tmp = replace_all(fragment_source, "#extension GL_OES_EGL_image_external : require\n", "");
    if (!tmp)
        return 2;
    render_source = replace_all(tmp, "samplerExternalOES", "sampler2D");
    free(tmp);
    if (!render_source)
        return 2;

    render_program = link_program(render_source);
    assert_no_tonemap_passthrough(render_program);
    assert_tonemap_is_monotonic(render_program);

    glDeleteProgram(exact_program);
    glDeleteProgram(render_program);
    free(render_source);
    free(fragment_source);
    eglTerminate(display);
    return 0;
}
