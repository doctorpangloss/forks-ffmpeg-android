/*
 * Android MediaCodec GLES surface processor
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "config.h"

#if CONFIG_MEDIACODEC

#define EGL_EGLEXT_PROTOTYPES 1
#define GL_GLEXT_PROTOTYPES 1

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <unistd.h>

#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"

#include "mediacodec_gl.h"

#ifndef EGL_RECORDABLE_ANDROID
#define EGL_RECORDABLE_ANDROID 0x3142
#endif

typedef struct MediaCodecGLProcessor {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLConfig  config;

    GLuint program;
    GLuint texture;
    GLint pos_loc;
    GLint tex_loc;
    GLint sampler_loc;
    GLint tonemap_loc;
    GLint trc_loc;
    GLint bt2020_loc;

    int width;
    int height;
} MediaCodecGLProcessor;

static const char *vertex_shader =
    "attribute vec4 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = position;\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

/*
 * HDR transfer, gamut conversion and Hable tone mapping follow the established
 * MediaCodec+OpenGL approach used by Grafika and the Apache-2.0 HDRSample
 * project, adapted here as GLSL ES 1.00 for FFmpeg's native MediaCodec path.
 */
static const char *fragment_shader =
    "#extension GL_OES_EGL_image_external : require\n"
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform samplerExternalOES inputImageTexture;\n"
    "uniform int u_tonemap;\n"
    "uniform int u_trc;\n"
    "uniform int u_bt2020;\n"
    "const mat3 bt2020_to_bt709 = mat3(\n"
    "    1.660491, -0.12455047, -0.01815076,\n"
    "   -0.58764114, 1.1328999,  -0.1005789,\n"
    "   -0.07284986,-0.00834942,  1.11872966);\n"
    "vec3 pq_eotf(vec3 color) {\n"
    "    const float m1 = 0.1593017578125;\n"
    "    const float m2 = 78.84375;\n"
    "    const float c1 = 0.8359375;\n"
    "    const float c2 = 18.8515625;\n"
    "    const float c3 = 18.6875;\n"
    "    vec3 p = pow(clamp(color, 0.0, 1.0), vec3(1.0 / m2));\n"
    "    return pow(max(p - vec3(c1), vec3(0.0)) / (vec3(c2) - vec3(c3) * p), vec3(1.0 / m1));\n"
    "}\n"
    "vec3 hlg_eotf(vec3 color) {\n"
    "    const float a = 0.17883277;\n"
    "    const float b = 0.28466892;\n"
    "    const float c = 0.55991073;\n"
    "    vec3 x = clamp(color, 0.0, 1.0);\n"
    "    vec3 inv = mix((x * x) / 3.0, (exp((x - vec3(c)) / a) + b) / 12.0, step(vec3(0.5), x));\n"
    "    float y = max(dot(inv, vec3(0.262700, 0.677998, 0.059302)), 0.000001);\n"
    "    return inv * pow(y, 0.2);\n"
    "}\n"
    "vec3 hable(vec3 x) {\n"
    "    const float A = 0.15;\n"
    "    const float B = 0.50;\n"
    "    const float C = 0.10;\n"
    "    const float D = 0.20;\n"
    "    const float E = 0.02;\n"
    "    const float F = 0.30;\n"
    "    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;\n"
    "}\n"
    "vec3 bt709_oetf(vec3 color) {\n"
    "    const float alpha = 1.09929682680944;\n"
    "    const float beta = 0.018053968510807;\n"
    "    vec3 x = max(color, vec3(0.0));\n"
    "    return mix(x * 4.5, alpha * pow(x, vec3(0.45)) - vec3(alpha - 1.0), step(vec3(beta), x));\n"
    "}\n"
    "void main() {\n"
    "    vec4 sample_color = texture2D(inputImageTexture, v_texcoord);\n"
    "    vec3 rgb = sample_color.rgb;\n"
    "    if (u_tonemap != 0) {\n"
    "        if (u_trc == 16)\n"
    "            rgb = pq_eotf(rgb) * 100.0;\n"
    "        else if (u_trc == 18)\n"
    "            rgb = hlg_eotf(rgb) * 12.0;\n"
    "        if (u_bt2020 != 0)\n"
    "            rgb = bt2020_to_bt709 * rgb;\n"
    "        rgb = hable(max(rgb, vec3(0.0)));\n"
    "        rgb = rgb / hable(vec3(11.2));\n"
    "        rgb = bt709_oetf(clamp(rgb, 0.0, 1.0));\n"
    "    } else if (u_bt2020 != 0) {\n"
    "        rgb = clamp(bt2020_to_bt709 * rgb, 0.0, 1.0);\n"
    "    }\n"
    "    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), sample_color.a);\n"
    "}\n";

static GLuint compile_shader(AVCodecContext *avctx, GLenum type, const char *source)
{
    GLint ok = 0;
    GLuint shader = glCreateShader(type);
    if (!shader)
        return 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(log), &len, log);
        av_log(avctx, AV_LOG_ERROR, "MediaCodec GLES shader compile failed: %.*s\n", len, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int init_gl(AVCodecContext *avctx, AVMediaCodecDeviceContext *device,
                   MediaCodecGLProcessor **out)
{
    static const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_RECORDABLE_ANDROID, EGL_TRUE,
        EGL_NONE
    };
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    MediaCodecGLProcessor *p;
    EGLint num_configs = 0;
    GLuint vs, fs;

    p = av_mallocz(sizeof(*p));
    if (!p)
        return AVERROR(ENOMEM);

    p->width = avctx->width;
    p->height = avctx->height;
    p->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (p->display == EGL_NO_DISPLAY || !eglInitialize(p->display, NULL, NULL))
        goto fail;
    if (!eglChooseConfig(p->display, config_attribs, &p->config, 1, &num_configs) || !num_configs)
        goto fail;
    p->context = eglCreateContext(p->display, p->config, EGL_NO_CONTEXT, context_attribs);
    if (p->context == EGL_NO_CONTEXT)
        goto fail;
    p->surface = eglCreateWindowSurface(p->display, p->config,
                                        device->encoder_native_window ?
                                        device->encoder_native_window :
                                        device->native_window, NULL);
    if (p->surface == EGL_NO_SURFACE)
        goto fail;
    if (!eglMakeCurrent(p->display, p->surface, p->surface, p->context))
        goto fail;

    vs = compile_shader(avctx, GL_VERTEX_SHADER, vertex_shader);
    fs = compile_shader(avctx, GL_FRAGMENT_SHADER, fragment_shader);
    if (!vs || !fs)
        goto fail;
    p->program = glCreateProgram();
    glAttachShader(p->program, vs);
    glAttachShader(p->program, fs);
    glLinkProgram(p->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!p->program)
        goto fail;

    p->pos_loc = glGetAttribLocation(p->program, "position");
    p->tex_loc = glGetAttribLocation(p->program, "texcoord");
    p->sampler_loc = glGetUniformLocation(p->program, "inputImageTexture");
    p->tonemap_loc = glGetUniformLocation(p->program, "u_tonemap");
    p->trc_loc = glGetUniformLocation(p->program, "u_trc");
    p->bt2020_loc = glGetUniformLocation(p->program, "u_bt2020");

    glGenTextures(1, &p->texture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, p->texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    *out = p;
    return 0;

fail:
    ff_mediacodec_gl_uninit((void **)&p);
    av_log(avctx, AV_LOG_ERROR, "Failed to initialize MediaCodec GLES surface processor\n");
    return AVERROR_EXTERNAL;
}

void ff_mediacodec_gl_uninit(void **opaque)
{
    MediaCodecGLProcessor *p;

    if (!opaque || !*opaque)
        return;

    p = *opaque;
    if (p->display != EGL_NO_DISPLAY)
        eglMakeCurrent(p->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (p->texture)
        glDeleteTextures(1, &p->texture);
    if (p->program)
        glDeleteProgram(p->program);
    if (p->display != EGL_NO_DISPLAY && p->surface != EGL_NO_SURFACE)
        eglDestroySurface(p->display, p->surface);
    if (p->display != EGL_NO_DISPLAY && p->context != EGL_NO_CONTEXT)
        eglDestroyContext(p->display, p->context);
    if (p->display != EGL_NO_DISPLAY)
        eglTerminate(p->display);
    av_freep(opaque);
}

static int acquire_latest_image(AImageReader *reader, AImage **image)
{
    media_status_t status;

    for (int i = 0; i < 50; i++) {
        status = AImageReader_acquireLatestImage(reader, image);
        if (status == AMEDIA_OK && *image)
            return 0;
        usleep(1000);
    }
    return AVERROR(EAGAIN);
}

int ff_mediacodec_gl_process(AVCodecContext *avctx,
                             AVMediaCodecDeviceContext *device,
                             AVMediaCodecBuffer *buffer,
                             const AVFrame *frame,
                             int tonemap)
{
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    static const GLfloat texcoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    };
    MediaCodecGLProcessor *p = (MediaCodecGLProcessor *)device->surface_processor;
    AImage *image = NULL;
    AHardwareBuffer *hardware_buffer = NULL;
    EGLClientBuffer client_buffer;
    EGLImageKHR egl_image;
    int64_t time;
    int ret;

    if (!device->image_reader)
        return AVERROR(EINVAL);
    if (!p) {
        ret = init_gl(avctx, device, &p);
        if (ret < 0)
            return ret;
        device->surface_processor = p;
    }

    time = av_rescale_q(frame->pts, avctx->time_base, (AVRational){ 1, 1000000000 });
    ret = av_mediacodec_render_buffer_at_time(buffer, time);
    if (ret < 0)
        return ret;

    ret = acquire_latest_image(device->image_reader, &image);
    if (ret < 0)
        return ret;

    ret = AImage_getHardwareBuffer(image, &hardware_buffer);
    if (ret != AMEDIA_OK || !hardware_buffer) {
        AImage_delete(image);
        return AVERROR_EXTERNAL;
    }

    eglMakeCurrent(p->display, p->surface, p->surface, p->context);
    client_buffer = eglGetNativeClientBufferANDROID(hardware_buffer);
    egl_image = eglCreateImageKHR(p->display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                  client_buffer, NULL);
    if (egl_image == EGL_NO_IMAGE_KHR) {
        AImage_delete(image);
        return AVERROR_EXTERNAL;
    }

    glViewport(0, 0, p->width, p->height);
    glUseProgram(p->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, p->texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, egl_image);
    glUniform1i(p->sampler_loc, 0);
    glUniform1i(p->tonemap_loc, tonemap);
    glUniform1i(p->trc_loc, frame->color_trc);
    glUniform1i(p->bt2020_loc, frame->colorspace == AVCOL_SPC_BT2020_NCL ||
                                frame->colorspace == AVCOL_SPC_BT2020_CL);
    glVertexAttribPointer(p->pos_loc, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(p->pos_loc);
    glVertexAttribPointer(p->tex_loc, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
    glEnableVertexAttribArray(p->tex_loc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(p->pos_loc);
    glDisableVertexAttribArray(p->tex_loc);
    eglPresentationTimeANDROID(p->display, p->surface, time);
    eglSwapBuffers(p->display, p->surface);
    eglDestroyImageKHR(p->display, egl_image);
    AImage_delete(image);

    return 0;
}

#endif /* CONFIG_MEDIACODEC */
