/*
 * Copyright 2011 VMWare, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author: Jakob Bornecrantz <wallbraker@gmail.com>
 * Author: Thomas Hellstrom <thellstrom@vmware.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include "vmwgfx_drm.h"
#include <xf86drm.h>
#include "vmwgfx_drmi.h"

#define uint32 uint32_t
#define int32 int32_t
#define uint16 uint16_t
#define uint8 uint8_t

#include "svga3d_reg.h"
#include "vmwgfx_driver.h"

static int
vmwgfx_fence_wait(int drm_fd, uint64_t seq)
{
	struct drm_vmw_fence_wait_arg farg;
	memset(&farg, 0, sizeof(farg));

	farg.sequence = seq;
	farg.cookie_valid = 0;

	return drmCommandWriteRead(drm_fd, DRM_VMW_FENCE_WAIT, &farg,
				   sizeof(farg));
}

int
vmwgfx_present_readback(int drm_fd, RegionPtr region)
{
    BoxPtr clips = REGION_RECTS(region);
    unsigned int num_clips = REGION_NUM_RECTS(region);
    struct drm_vmw_execbuf_arg arg;
    struct drm_vmw_fence_rep rep;
    int ret;
    unsigned int size;
    unsigned i;
    SVGA3dRect *cr;

    struct {
	SVGA3dCmdHeader header;
	SVGA3dRect cr;
    } *cmd;

    if (num_clips == 0)
	return 0;

    size = sizeof(*cmd) + (num_clips - 1) * sizeof(cmd->cr);
    cmd = malloc(size);
    if (!cmd)
	return -1;

    cmd->header.id = SVGA_3D_CMD_PRESENT_READBACK;
    cmd->header.size = num_clips * sizeof(cmd->cr);

    for (i=0, cr = &cmd->cr; i < num_clips; i++, cr++, clips++) {
	cr->x = (uint16_t) clips->x1;
	cr->y = (uint16_t) clips->y1;
	cr->w = (uint16_t) (clips->x2 - clips->x1);
	cr->h = (uint16_t) (clips->y2 - clips->y1);
#if 0
	LogMessage(X_INFO,
		   "Readback x: %u y: %u srcx: %u srcy: %u w: %u h: %u\n",
		   cr->x, cr->y, cr->x, cr->y, cr->w, cr->h);
#endif
    }


    memset(&arg, 0, sizeof(arg));
    memset(&rep, 0, sizeof(rep));

    rep.error = -EFAULT;
    arg.fence_rep = (unsigned long)&rep;
    arg.commands = (unsigned long)cmd;
    arg.command_size = size;
    arg.throttle_us = 0;

    ret = drmCommandWrite(drm_fd, DRM_VMW_EXECBUF, &arg, sizeof(arg));
    if (ret)
	LogMessage(X_ERROR, "Present readback error %s.\n", strerror(-ret));

    free(cmd);

    /*
     * Sync to avoid racing with Xorg SW rendering.
     */

    if (rep.error == 0) {
	ret = vmwgfx_fence_wait(drm_fd, rep.fence_seq);
	if (ret)
	    LogMessage(X_ERROR, "Present readback fence wait error %s.\n",
		       strerror(-ret));
    }

    return 0;
}

int
vmwgfx_present(int drm_fd, unsigned int dst_x, unsigned int dst_y,
	       RegionPtr region, uint32_t handle)
{
    BoxPtr clips = REGION_RECTS(region);
    unsigned int num_clips = REGION_NUM_RECTS(region);
    struct drm_vmw_execbuf_arg arg;
    struct drm_vmw_fence_rep rep;
    int ret;
    unsigned int size;
    unsigned i;
    SVGA3dCopyRect *cr;

    struct {
	SVGA3dCmdHeader header;
	SVGA3dCmdPresent body;
	SVGA3dCopyRect cr;
    } *cmd;

    if (num_clips == 0)
	return 0;

    size = sizeof(*cmd) + (num_clips - 1) * sizeof(cmd->cr);
    cmd = malloc(size);
    if (!cmd)
	return -1;

    cmd->header.id = SVGA_3D_CMD_PRESENT;
    cmd->header.size = sizeof(cmd->body) + num_clips * sizeof(cmd->cr);
    cmd->body.sid = handle;


    for (i=0, cr = &cmd->cr; i < num_clips; i++, cr++, clips++) {
	cr->x = (uint16_t) clips->x1 + dst_x;
	cr->y = (uint16_t) clips->y1 + dst_y;
	cr->srcx = (uint16_t) clips->x1;
	cr->srcy = (uint16_t) clips->y1;
	cr->w = (uint16_t) (clips->x2 - clips->x1);
	cr->h = (uint16_t) (clips->y2 - clips->y1);
#if 0
	LogMessage(X_INFO, "Present: x: %u y: %u srcx: %u srcy: %u w: %u h: %u\n",
		   cr->x, cr->y, cr->srcx, cr->srcy, cr->w, cr->h);
#endif
    }

    memset(&arg, 0, sizeof(arg));
    memset(&rep, 0, sizeof(rep));

    rep.error = -EFAULT;
    arg.fence_rep = (unsigned long)&rep;
    arg.commands = (unsigned long)cmd;
    arg.command_size = size;
    arg.throttle_us = 0;

    ret = drmCommandWrite(drm_fd, DRM_VMW_EXECBUF, &arg, sizeof(arg));
    if (ret) {
	LogMessage(X_ERROR, "Present error %s.\n", strerror(-ret));
    }

    free(cmd);
    return 0;
}

struct vmwgfx_int_dmabuf {
    struct vmwgfx_dmabuf buf;
    uint64_t map_handle;
    uint64_t sync_handle;
    int sync_valid;
    int drm_fd;
    uint32_t map_count;
    void *addr;
};

static inline struct vmwgfx_int_dmabuf *
vmwgfx_int_dmabuf(struct vmwgfx_dmabuf *buf)
{
    return (struct vmwgfx_int_dmabuf *) buf;
}

struct vmwgfx_dmabuf*
vmwgfx_dmabuf_alloc(int drm_fd, size_t size)
{
    union drm_vmw_alloc_dmabuf_arg arg;
    struct vmwgfx_dmabuf *buf;
    struct vmwgfx_int_dmabuf *ibuf;
    int ret;

    ibuf = calloc(1, sizeof(*ibuf));
    if (!ibuf)
	return NULL;

    buf = &ibuf->buf;
    memset(&arg, 0, sizeof(arg));
    arg.req.size = size;

    ret = drmCommandWriteRead(drm_fd, DRM_VMW_ALLOC_DMABUF, &arg,
			      sizeof(arg));
    if (ret)
	goto out_kernel_fail;

    ibuf = vmwgfx_int_dmabuf(buf);
    ibuf->map_handle = arg.rep.map_handle;
    ibuf->drm_fd = drm_fd;
    buf->handle = arg.rep.handle;
    buf->gmr_id = arg.rep.cur_gmr_id;
    buf->gmr_offset = arg.rep.cur_gmr_offset;
    buf->size = size;

    return buf;
  out_kernel_fail:
    free(buf);
    return NULL;
}

void *
vmwgfx_dmabuf_map(struct vmwgfx_dmabuf *buf)
{
    struct vmwgfx_int_dmabuf *ibuf = vmwgfx_int_dmabuf(buf);

    if (ibuf->addr)
	return ibuf->addr;

    ibuf->addr =  mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       ibuf->drm_fd, ibuf->map_handle);

    if (ibuf->addr == MAP_FAILED) {
	ibuf->addr = NULL;
	return NULL;
    }

    ibuf->map_count++;
    return ibuf->addr;
}

void
vmwgfx_dmabuf_unmap(struct vmwgfx_dmabuf *buf)
{
    struct vmwgfx_int_dmabuf *ibuf = vmwgfx_int_dmabuf(buf);

    if (--ibuf->map_count)
	return;

    /*
     * It's a pretty important performance optimzation not to call
     * munmap here, although we should watch out for cases where we might fill
     * the virtual memory space of the process.
     */
}

void
vmwgfx_dmabuf_destroy(struct vmwgfx_dmabuf *buf)
{
    struct vmwgfx_int_dmabuf *ibuf = vmwgfx_int_dmabuf(buf);
    struct drm_vmw_unref_dmabuf_arg arg;

    if (ibuf->addr) {
	munmap(ibuf->addr, buf->size);
	ibuf->addr = NULL;
    }

    memset(&arg, 0, sizeof(arg));
    arg.handle = buf->handle;

    (void) drmCommandWrite(ibuf->drm_fd, DRM_VMW_UNREF_DMABUF, &arg,
			   sizeof(arg));
    free(buf);
}

int
vmwgfx_dma(unsigned int host_x, unsigned int host_y,
	   RegionPtr region, struct vmwgfx_dmabuf *buf,
	   uint32_t buf_pitch, uint32_t surface_handle, int to_surface)
{
    BoxPtr clips = REGION_RECTS(region);
    unsigned int num_clips = REGION_NUM_RECTS(region);
    struct drm_vmw_execbuf_arg arg;
    struct drm_vmw_fence_rep rep;
    int ret;
    unsigned int size;
    unsigned i;
    SVGA3dCopyBox *cb;
    SVGA3dCmdSurfaceDMASuffix *suffix;
    SVGA3dCmdSurfaceDMA *body;
    struct vmwgfx_int_dmabuf *ibuf = vmwgfx_int_dmabuf(buf);

    struct {
	SVGA3dCmdHeader header;
	SVGA3dCmdSurfaceDMA body;
	SVGA3dCopyBox cb;
    } *cmd;

    if (num_clips == 0)
	return 0;

    size = sizeof(*cmd) + (num_clips - 1) * sizeof(cmd->cb) +
	sizeof(*suffix);
    cmd = malloc(size);
    if (!cmd)
	return -1;

    cmd->header.id = SVGA_3D_CMD_SURFACE_DMA;
    cmd->header.size = sizeof(cmd->body) + num_clips * sizeof(cmd->cb) +
	sizeof(*suffix);
    cb = &cmd->cb;

    suffix = (SVGA3dCmdSurfaceDMASuffix *) &cb[num_clips];
    suffix->suffixSize = sizeof(*suffix);
    suffix->maximumOffset = (uint32_t) -1;
    suffix->flags.discard = 0;
    suffix->flags.unsynchronized = 0;
    suffix->flags.reserved = 0;

    body = &cmd->body;
    body->guest.ptr.gmrId = buf->gmr_id;
    body->guest.ptr.offset = buf->gmr_offset;
    body->guest.pitch = buf_pitch;
    body->host.sid = surface_handle;
    body->host.face = 0;
    body->host.mipmap = 0;

    body->transfer =  (to_surface ? SVGA3D_WRITE_HOST_VRAM :
		       SVGA3D_READ_HOST_VRAM);


    for (i=0; i < num_clips; i++, cb++, clips++) {
	cb->x = (uint16_t) clips->x1 + host_x;
	cb->y = (uint16_t) clips->y1 + host_y;
	cb->z = 0;
	cb->srcx = (uint16_t) clips->x1;
	cb->srcy = (uint16_t) clips->y1;
	cb->srcz = 0;
	cb->w = (uint16_t) (clips->x2 - clips->x1);
	cb->h = (uint16_t) (clips->y2 - clips->y1);
	cb->d = 1;
#if 0
	LogMessage(X_INFO, "DMA! x: %u y: %u srcx: %u srcy: %u w: %u h: %u %s\n",
		   cb->x, cb->y, cb->srcx, cb->srcy, cb->w, cb->h,
		   to_surface ? "to" : "from");
#endif

    }

    memset(&arg, 0, sizeof(arg));
    memset(&rep, 0, sizeof(rep));

    rep.error = -EFAULT;
    arg.fence_rep = (unsigned long)&rep;
    arg.commands = (unsigned long)cmd;
    arg.command_size = size;
    arg.throttle_us = 0;

    ret = drmCommandWrite(ibuf->drm_fd, DRM_VMW_EXECBUF, &arg, sizeof(arg));
    if (ret) {
	LogMessage(X_ERROR, "DMA error %s.\n", strerror(-ret));
    }

    free(cmd);

    if (!to_surface && rep.error == 0) {
	ret = vmwgfx_fence_wait(ibuf->drm_fd, rep.fence_seq);
	if (ret)
	    LogMessage(X_ERROR, "DMA from host fence wait error %s.\n",
		       strerror(-ret));
    }

    return 0;
}

static int
vmwgfx_get_param(int drm_fd, uint32_t param, uint64_t *out)
{
    struct drm_vmw_getparam_arg gp_arg;
    int ret;

    memset(&gp_arg, 0, sizeof(gp_arg));
    gp_arg.param = param;
    ret = drmCommandWriteRead(drm_fd, DRM_VMW_GET_PARAM,
	    &gp_arg, sizeof(gp_arg));

    if (ret == 0) {
	*out = gp_arg.value;
    }

    return ret;
}

int
vmwgfx_num_streams(int drm_fd, uint32_t *ntot, uint32_t *nfree)
{
    uint64_t v1, v2;
    int ret;

    ret = vmwgfx_get_param(drm_fd, DRM_VMW_PARAM_NUM_STREAMS, &v1);
    if (ret)
	return ret;

    ret = vmwgfx_get_param(drm_fd, DRM_VMW_PARAM_NUM_FREE_STREAMS, &v2);
    if (ret)
	return ret;

    *ntot = (uint32_t)v1;
    *nfree = (uint32_t)v2;

    return 0;
}

int
vmwgfx_claim_stream(int drm_fd, uint32_t *out)
{
    struct drm_vmw_stream_arg s_arg;
    int ret;

    ret = drmCommandRead(drm_fd, DRM_VMW_CLAIM_STREAM,
			 &s_arg, sizeof(s_arg));

    if (ret)
	return -1;

    *out = s_arg.stream_id;
    return 0;
}

int
vmwgfx_unref_stream(int drm_fd, uint32_t stream_id)
{
    struct drm_vmw_stream_arg s_arg;
    int ret;

    memset(&s_arg, 0, sizeof(s_arg));
    s_arg.stream_id = stream_id;

    ret = drmCommandWrite(drm_fd, DRM_VMW_UNREF_STREAM,
			  &s_arg, sizeof(s_arg));

    return 0;
}

int
vmwgfx_cursor_bypass(int drm_fd, int xhot, int yhot)
{
    struct drm_vmw_cursor_bypass_arg arg;
    int ret;

    memset(&arg, 0, sizeof(arg));
    arg.flags = DRM_VMW_CURSOR_BYPASS_ALL;
    arg.xhot = xhot;
    arg.yhot = yhot;

    ret = drmCommandWrite(drm_fd, DRM_VMW_CURSOR_BYPASS,
			  &arg, sizeof(arg));

    return ret;
}

int
vmwgfx_max_fb_size(int drm_fd, size_t *size)
{
    drmVersionPtr ver;
    int ret = -1;
    uint64_t tmp_size;

    ver = drmGetVersion(drm_fd);
    if (ver == NULL ||
	!(ver->version_major > 1 ||
	  (ver->version_major == 1 && ver->version_minor >= 3)))
	goto out;

    if (vmwgfx_get_param(drm_fd, DRM_VMW_PARAM_MAX_FB_SIZE, &tmp_size) != 0)
	goto out;

    *size = tmp_size;
    ret = 0;

  out:
    drmFreeVersion(ver);
    return ret;
}
