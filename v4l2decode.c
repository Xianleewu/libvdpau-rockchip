#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <linux/videodev2.h>

#include "vdpau_private.h"

#include "v4l2.h"
#include "v4l2decode.h"

static void openDevices(v4l2_decoder_t *ctx);
static void cleanup(v4l2_decoder_t *ctx);

static __u32 get_codec(VdpDecoderProfile profile)
{
    switch (profile)
    {
    case VDP_DECODER_PROFILE_MPEG1:
        return V4L2_PIX_FMT_MPEG1;

    case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
    case VDP_DECODER_PROFILE_MPEG2_MAIN:
        return V4L2_PIX_FMT_MPEG2;

    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH:
        return V4L2_PIX_FMT_H264;

    case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
    case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
        return V4L2_PIX_FMT_MPEG4;
    }
    //            return V4L2_PIX_FMT_H263;
    //            return V4L2_PIX_FMT_XVID;
    return V4L2_PIX_FMT_H264;
}

static parser_mode_t get_mode(VdpDecoderProfile profile)
{
    switch (profile)
    {
    case VDP_DECODER_PROFILE_MPEG1:
    case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
    case VDP_DECODER_PROFILE_MPEG2_MAIN:
        return MODE_MPEG12;

    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH:
        return MODE_H264;

    case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
    case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
        return MODE_MPEG4;
    }
    //            return V4L2_PIX_FMT_H263;
    //            return V4L2_PIX_FMT_XVID;
    return MODE_H264;
}


void *decoder_open(VdpDecoderProfile profile)
{
    v4l2_decoder_t *ctx = calloc(1, sizeof(v4l2_decoder_t));
    struct v4l2_format fmt;

    ctx->decoderHandle = -1;

    ctx->outputBuffersCount = -1;
    ctx->captureBuffersCount = -1;

    ctx->mode = get_mode(profile);
    ctx->codec = get_codec(profile);
    openDevices(ctx);

    ctx->parser = parse_stream_init(ctx->mode, STREAM_BUFFER_SIZE);

    // Setup mfc output
    // Set mfc output format
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.pixelformat = ctx->codec;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = STREAM_BUFFER_SIZE;
    fmt.fmt.pix_mp.num_planes = 1;
    if (ioctl(ctx->decoderHandle, VIDIOC_S_FMT, &fmt)) {
        VDPAU_DBG("Failed to setup for MFC decoding");
        cleanup(ctx);
        return NULL;
    }

    // Get mfc output format
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_FMT, &fmt)) {
        VDPAU_DBG("Get Format failed");
        cleanup(ctx);
        return NULL;
    }
    VDPAU_DBG("Setup MFC decoding buffer size=%u (requested=%u)", fmt.fmt.pix_mp.plane_fmt[0].sizeimage, STREAM_BUFFER_SIZE);

    // Request mfc output buffers
    ctx->outputBuffersCount = RequestBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, STREAM_BUFFER_CNT);
    if (ctx->outputBuffersCount == V4L2_ERROR) {
        VDPAU_DBG("REQBUFS failed on queue of MFC");
        cleanup(ctx);
        return NULL;
    }
    VDPAU_DBG("REQBUFS Number of MFC buffers is %d (requested %d)", ctx->outputBuffersCount, STREAM_BUFFER_CNT);

    // Memory Map mfc output buffers
    ctx->outputBuffers = (v4l2_buffer_t *)calloc(ctx->outputBuffersCount, sizeof(v4l2_buffer_t));
    if(!ctx->outputBuffers) {
        VDPAU_DBG("cannot allocate buffers\n");
        cleanup(ctx);
        return NULL;
    }
    if(!MmapBuffers(ctx->decoderHandle, ctx->outputBuffersCount, ctx->outputBuffers, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, FALSE)) {
        VDPAU_DBG("cannot mmap output buffers\n");
        cleanup(ctx);
        return NULL;
    }
    VDPAU_DBG("Succesfully mmapped %d buffers", ctx->outputBuffersCount);

    return ctx;
}

void decoder_close(void *private)
{
    v4l2_decoder_t *ctx = (v4l2_decoder_t*)private;
    cleanup(ctx);

    free(ctx->parser);
    free(ctx);
}

static void listFormats(v4l2_decoder_t *ctx)
{
    // we enumerate all the supported formats looking for NV12MT and NV12
    int index = 0;
    int ret   = -1;
    while (1) {
        struct v4l2_fmtdesc vid_fmtdesc = {};
        vid_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        vid_fmtdesc.index = index++;

        ret = ioctl(ctx->decoderHandle, VIDIOC_ENUM_FMT, &vid_fmtdesc);
        if (ret != 0)
            break;
        VDPAU_DBG("Decoder format %d: %c%c%c%c (%s)", vid_fmtdesc.index,
            vid_fmtdesc.pixelformat & 0xFF, (vid_fmtdesc.pixelformat >> 8) & 0xFF,
            (vid_fmtdesc.pixelformat >> 16) & 0xFF, (vid_fmtdesc.pixelformat >> 24) & 0xFF,
            vid_fmtdesc.description);
        if (vid_fmtdesc.pixelformat == V4L2_PIX_FMT_NV12MT)
            ;
        if (vid_fmtdesc.pixelformat == V4L2_PIX_FMT_NV12)
            ;
    }
}

static void openDevices(v4l2_decoder_t *ctx)
{
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir ("/sys/class/video4linux/")) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
            if (strncmp(ent->d_name, "video", 5) == 0) {
                char *p;
                char name[64];
                char devname[64];
                char sysname[64];
                char drivername[32];
                char target[1024];
                int ret;

                snprintf(sysname, 64, "/sys/class/video4linux/%s", ent->d_name);
                snprintf(name, 64, "/sys/class/video4linux/%s/name", ent->d_name);

                FILE* fp = fopen(name, "r");
                if (fgets(drivername, 32, fp) != NULL) {
                    p = strchr(drivername, '\n');
                    if (p != NULL)
                        *p = '\0';
                } else {
                    fclose(fp);
                    continue;
                }
                fclose(fp);

                ret = readlink(sysname, target, sizeof(target));
                if (ret < 0)
                    continue;
                target[ret] = '\0';
                p = strrchr(target, '/');
                if (p == NULL)
                    continue;

                sprintf(devname, "/dev/%s", ++p);

                if (ctx->decoderHandle < 0 && strstr(drivername, "s5p-mfc-dec") != NULL) {
                    struct v4l2_capability cap;
                    int fd = open(devname, O_RDWR | O_NONBLOCK, 0);
                    if (fd > 0) {
                        memzero(cap);
                        ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
                        if (ret == 0)
                            if ((cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE ||
                                    ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) && (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE))) &&
                                    (cap.capabilities & V4L2_CAP_STREAMING)) {
                                ctx->decoderHandle = fd;
                                VDPAU_DBG("Found %s %s", drivername, devname);
                            }
                  }
                  if (ctx->decoderHandle < 0)
                      close(fd);
                }
                if (ctx->decoderHandle >= 0) {
                    listFormats(ctx);
                    break;
                }
            }
        }
        closedir (dir);
    }
  return;
}

static void cleanup(v4l2_decoder_t *ctx)
{
    VDPAU_DBG("MUnmapping buffers");
    if (ctx->outputBuffers)
        ctx->outputBuffers = FreeBuffers(ctx->outputBuffersCount, ctx->outputBuffers);
    if (ctx->captureBuffers)
        ctx->captureBuffers = FreeBuffers(ctx->captureBuffersCount, ctx->captureBuffers);
    VDPAU_DBG("Devices cleanup");
    if (StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMOFF))
        VDPAU_DBG("Stream OFF");
    if (StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMOFF))
        VDPAU_DBG("Stream OFF");
    VDPAU_DBG("Closing devices");
    if (ctx->decoderHandle >= 0)
        close(ctx->decoderHandle);
}

static int process_header(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers);

static VdpStatus process_frames(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers, video_surface_ctx_t *output);

VdpStatus decoder_decode(void *private, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers, video_surface_ctx_t *output)
{
    v4l2_decoder_t *ctx = (v4l2_decoder_t*)private;

    if (!ctx->headerProcessed) {
        int ret = process_header(ctx, buffer_count, buffers);
        if(ret)
            return ret < 0 ? VDP_STATUS_ERROR : VDP_STATUS_OK;
        if (ctx->codec == V4L2_PIX_FMT_H263)
            return process_frames(ctx, buffer_count, buffers, output);
        return VDP_STATUS_OK;
    }

    return process_frames(ctx, buffer_count, buffers, output);
}

static int process_header(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers)
{
    int size, ret, i;
    struct v4l2_format fmt;
    struct v4l2_control ctrl;
    struct v4l2_crop crop;

    int capturePlane1Size;
    int capturePlane2Size;
    int capturePlane3Size;

    for(i=0 ; i<buffer_count ; i++)
        parse_push(ctx->parser, buffers[i].bitstream, buffers[i].bitstream_bytes);

    ret = parse_stream(ctx->parser, ctx->outputBuffers[0].cPlane[0], STREAM_BUFFER_SIZE, &size, TRUE);
    if(ret <= 0)
        return ret;

    // Prepare header frame
    ctx->outputBuffers[0].iBytesUsed[0] = size;

    // Queue header to mfc output
    ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, ctx->outputBuffers[0].iNumPlanes, 0, &ctx->outputBuffers[0]);
    if (ret == V4L2_ERROR) {
        VDPAU_DBG("queue input buffer");
        return -1;
    }
    ctx->outputBuffers[ret].bQueue = TRUE;
    VDPAU_DBG("<- %d header of size %d", ret, size);

    // STREAMON on mfc OUTPUT
    if (StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON))
        VDPAU_DBG("Stream ON");
    else {
        VDPAU_DBG("Failed to Stream ON");
        return -1;
    }

    // Setup mfc capture
    // Get mfc capture picture format
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_FMT, &fmt)) {
        VDPAU_DBG("Failed to get format from");
        return -1;
    }
    capturePlane1Size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    capturePlane2Size = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;
    capturePlane3Size = fmt.fmt.pix_mp.plane_fmt[2].sizeimage;
    VDPAU_DBG("G_FMT: fmt (%dx%d), plane[0]=%d plane[1]=%d plane[2]=%d", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, capturePlane1Size, capturePlane2Size, capturePlane3Size);

    // Get mfc needed number of buffers
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_CTRL, &ctrl)) {
        VDPAU_DBG("Failed to get the number of buffers required");
        return -1;
    }
    ctx->captureBuffersCount = ctrl.value + CAPTURE_EXTRA_BUFFER_CNT;

    // Get mfc capture crop
    memzero(crop);
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_CROP, &crop)) {
        VDPAU_DBG("Failed to get crop information");
        return -1;
    }
    VDPAU_DBG("G_CROP %dx%d", crop.c.width, crop.c.height);
    ctx->captureWidth = crop.c.width;
    ctx->captureHeight = crop.c.height;

    // Request mfc capture buffers
    ctx->captureBuffersCount = RequestBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, ctx->captureBuffersCount);
    if (ctx->captureBuffersCount == V4L2_ERROR) {
        VDPAU_DBG("REQBUFS failed");
        return -1;
    }
    VDPAU_DBG("REQBUFS Number of buffers is %d (extra %d)", ctx->captureBuffersCount, CAPTURE_EXTRA_BUFFER_CNT);

    // Memory Map and queue mfc capture buffers
    ctx->captureBuffers = (v4l2_buffer_t *)calloc(ctx->captureBuffersCount, sizeof(v4l2_buffer_t));
    if(!ctx->captureBuffers) {
        VDPAU_DBG("cannot allocate buffers");
        return -1;
    }
    if(!MmapBuffers(ctx->decoderHandle, ctx->captureBuffersCount, ctx->captureBuffers, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, TRUE)) {
        VDPAU_DBG("cannot mmap capture buffers");
        return -1;
    }
    for (i = 0; i < ctx->captureBuffersCount; i++) {
        ctx->captureBuffers[i].iBytesUsed[0] = capturePlane1Size;
        ctx->captureBuffers[i].iBytesUsed[1] = capturePlane2Size;
        ctx->captureBuffers[i].bQueue = TRUE;
    }
    VDPAU_DBG("Succesfully mmapped and queued %d buffers", ctx->captureBuffersCount);

    // STREAMON on mfc CAPTURE
    if (StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMON))
        VDPAU_DBG("Stream ON");
    else
        VDPAU_DBG("Failed to Stream ON");

    // Dequeue header on input queue
    ret = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
    if (ret < 0) {
        VDPAU_DBG("error dequeue output buffer, got number %d, errno %d", ret, errno);
        return -1;
    } else {
        VDPAU_DBG("-> %d header", ret);
        ctx->outputBuffers[ret].bQueue = FALSE;
    }
    ctx->headerProcessed = TRUE;
    return 0;
}

static VdpStatus process_frames(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers, video_surface_ctx_t *output)
{
    // MAIN LOOP
    int index = 0;
    int ret, i;

    for(i=0 ; i<buffer_count ; i++)
        parse_push(ctx->parser, buffers[i].bitstream, buffers[i].bitstream_bytes);

    while (index < ctx->outputBuffersCount && ctx->outputBuffers[index].bQueue)
        index++;

    if (index >= ctx->outputBuffersCount) { //all input buffers are busy, dequeue needed
        ret = PollOutput(ctx->decoderHandle, 1000); // POLLIN - Poll Capture, POLLOUT - Poll Output
        if (ret == V4L2_ERROR) {
            VDPAU_DBG("PollInput Error");
            return VDP_STATUS_ERROR;
        } else if (ret == V4L2_READY) {
            index = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
            if (index < 0) {
                VDPAU_DBG("error dequeue output buffer, got number %d, errno %d", index, errno);
                return VDP_STATUS_ERROR;
            } else {
                VDPAU_DBG("-> %d", index);
                ctx->outputBuffers[index].bQueue = FALSE;
            }
        } else {
            VDPAU_DBG("What the? %d", ret);
            return VDP_STATUS_ERROR;
        }
    }

    // Parse frame, copy it to buffer
    int frameSize = 0;
    ret = parse_stream(ctx->parser, ctx->outputBuffers[index].cPlane[0], STREAM_BUFFER_SIZE, &frameSize, FALSE);
    if (ret == 0)
        return VDP_STATUS_OK;
    if (ret < 0)
        return VDP_STATUS_ERROR;
    VDPAU_DBG("Extracted frame of size %d", frameSize);
    ctx->outputBuffers[index].iBytesUsed[0] = frameSize;

    // Queue buffer into input queue
    ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, ctx->outputBuffers[index].iNumPlanes, index, &ctx->outputBuffers[index]);
    if (ret == V4L2_ERROR) {
        VDPAU_DBG("Failed to queue buffer with index %d, errno %d", index, errno);
        return VDP_STATUS_ERROR;
    } else {
        ctx->outputBuffers[index].bQueue = TRUE;
        VDPAU_DBG("%d <-", index);
    }

    index = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
    if (index < 0) {
        if (index == -EAGAIN) {// Dequeue buffer not ready, need more data on input. EAGAIN = 11
            VDPAU_DBG("again...");
            return VDP_STATUS_OK;
        }
        VDPAU_DBG("error dequeue output buffer, got number %d", index);
        return VDP_STATUS_ERROR;
    } else {
        VDPAU_DBG("-> %d", index);
        ctx->captureBuffers[index].bQueue = FALSE;
    }

    // TODO send to video surface

    ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, ctx->captureBuffers[index].iNumPlanes, index, &ctx->captureBuffers[index]);
    if (ret == V4L2_ERROR) {
        VDPAU_DBG("Failed to queue buffer with index %d, errno = %d", index, errno);
        return VDP_STATUS_ERROR;
    } else {
        VDPAU_DBG("<- %d", ret);
        ctx->captureBuffers[ret].bQueue = TRUE;
    }

    return VDP_STATUS_OK;
}