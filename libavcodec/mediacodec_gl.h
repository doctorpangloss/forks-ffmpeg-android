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

#ifndef AVCODEC_MEDIACODEC_GL_H
#define AVCODEC_MEDIACODEC_GL_H

#include "avcodec.h"
#include "mediacodec.h"
#include "libavutil/hwcontext_mediacodec.h"

int ff_mediacodec_gl_process(AVCodecContext *avctx,
                             AVMediaCodecDeviceContext *device,
                             AVMediaCodecBuffer *buffer,
                             const AVFrame *frame,
                             int tonemap);

void ff_mediacodec_gl_uninit(void **opaque);

#endif /* AVCODEC_MEDIACODEC_GL_H */
