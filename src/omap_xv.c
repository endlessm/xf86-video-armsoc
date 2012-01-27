/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2012 Texas Instruments, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <rob@ti.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#include "fourcc.h"

#include "omap_driver.h"
#include "omap_exa.h"

#define NUM_TEXTURE_PORTS 32		/* this is basically arbitrary */

#define IMAGE_MAX_W 2048
#define IMAGE_MAX_H 2048

typedef struct {
	unsigned int format;
	int nplanes;
	PixmapPtr pSrcPix[3];
} OMAPPortPrivRec, *OMAPPortPrivPtr;


static XF86VideoEncodingRec OMAPVideoEncoding[] =
{
		{ 0, (char *)"XV_IMAGE", IMAGE_MAX_W, IMAGE_MAX_H, {1, 1} },
};

static XF86VideoFormatRec OMAPVideoFormats[] =
{
		{15, TrueColor}, {16, TrueColor}, {24, TrueColor},
		{15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};

static XF86AttributeRec OMAPVideoTexturedAttributes[] =
{
};

static XF86ImageRec OMAPVideoTexturedImages[MAX_FORMATS];



static PixmapPtr
setupplane(ScreenPtr pScreen, PixmapPtr pSrcPix, int width, int height,
		int depth, int srcpitch, int bufpitch, unsigned char **bufp)
{
	struct omap_bo *bo;
	unsigned char *src, *buf = *bufp;
	int i;

	if (pSrcPix && ((pSrcPix->drawable.height != height) ||
			(pSrcPix->drawable.width != width))) {
		pScreen->DestroyPixmap(pSrcPix);
		pSrcPix = NULL;
	}

	if (!pSrcPix) {
		pSrcPix = pScreen->CreatePixmap(pScreen, width, height, depth, 0);
	}

	bo = OMAPPixmapBo(pSrcPix);
	omap_bo_cpu_prep(bo, OMAP_GEM_WRITE);
	src = omap_bo_map(bo);

	/* copy from buf to src pixmap: */
	for (i = 0; i < height; i++) {
		memcpy(src, buf, srcpitch);
		src += srcpitch;
		buf += bufpitch;
	}

	omap_bo_cpu_fini(bo, OMAP_GEM_WRITE);

	*bufp = buf;

	return pSrcPix;
}

static void
freebufs(ScreenPtr pScreen, OMAPPortPrivPtr pPriv)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pPriv->pSrcPix); i++) {
		if (pPriv->pSrcPix[i])
			pScreen->DestroyPixmap(pPriv->pSrcPix[i]);
		pPriv->pSrcPix[i] = NULL;
	}
}


static void
OMAPVideoStopVideo(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
	/* maybe we can deallocate pSrcPix here?? */
}

static int
OMAPVideoSetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
		INT32 value, pointer data)
{
	return BadMatch;
}

static int
OMAPVideoGetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
		INT32 *value, pointer data)
{
	return BadMatch;
}

static void
OMAPVideoQueryBestSize(ScrnInfoPtr pScrn, Bool motion,
		short vid_w, short vid_h,
		short drw_w, short drw_h,
		unsigned int *p_w, unsigned int *p_h,
		pointer data)
{
	/* currently no constraints.. */
	*p_w = drw_w;
	*p_h = drw_h;
}

static int OMAPVideoPutTextureImage(
		PixmapPtr pSrcPix, BoxPtr pSrcBox,
		PixmapPtr pOsdPix, BoxPtr pOsdBox,
		PixmapPtr pDstPix, BoxPtr pDstBox,
		void *closure)
{
	ScreenPtr pScreen = pDstPix->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	OMAPPortPrivPtr pPriv = closure;
	Bool ret;

	DEBUG_MSG("src: %dx%d; %d,%d %d,%d",
			pSrcPix->drawable.width, pSrcPix->drawable.height,
			pSrcBox->x1, pSrcBox->y1, pSrcBox->x2, pSrcBox->y2);
	DEBUG_MSG("dst: %dx%d; %d,%d %d,%d",
			pDstPix->drawable.width, pDstPix->drawable.height,
			pDstBox->x1, pDstBox->y1, pDstBox->x2, pDstBox->y2);

	ret = pOMAP->pOMAPEXA->PutTextureImage(pSrcPix, pSrcBox,
			pOsdPix, pOsdBox, pDstPix, pDstBox,
			pPriv->nplanes - 1, &pPriv->pSrcPix[1],
			pPriv->format);
	if (ret) {
		return Success;
	}
	DEBUG_MSG("PutTextureImage failed");

	return BadImplementation;
}

/**
 * The main function for XV, called to blit/scale/colorcvt an image
 * to it's destination drawable
 *
 * The source rectangle of the video is defined by (src_x, src_y, src_w, src_h).
 * The dest rectangle of the video is defined by (drw_x, drw_y, drw_w, drw_h).
 * id is a fourcc code for the format of the video.
 * buf is the pointer to the source data in system memory.
 * width and height are the w/h of the source data.
 * If "sync" is TRUE, then we must be finished with *buf at the point of return
 * (which we always are).
 * clipBoxes is the clipping region in screen space.
 * data is a pointer to our port private.
 * drawable is some Drawable, which might not be the screen in the case of
 * compositing.  It's a new argument to the function in the 1.1 server.
 */
static int
OMAPVideoPutImage(ScrnInfoPtr pScrn, short src_x, short src_y, short drw_x,
		short drw_y, short src_w, short src_h, short drw_w, short drw_h,
		int id, unsigned char *buf, short width, short height,
		Bool Sync, RegionPtr clipBoxes, pointer data, DrawablePtr pDstDraw)
{
	ScreenPtr pScreen = pDstDraw->pScreen;
	OMAPPortPrivPtr pPriv = (OMAPPortPrivPtr)data;
	BoxRec srcb = {
			.x1 = src_x,
			.y1 = src_y,
			.x2 = src_x + src_w,
			.y2 = src_y + src_h,
	};
	BoxRec dstb = {
			.x1 = drw_x,
			.y1 = drw_y,
			.x2 = drw_x + drw_w,
			.y2 = drw_y + drw_h,
	};
	int i, depth, nplanes;
	int srcpitch1, srcpitch2, bufpitch1, bufpitch2, src_h2, src_w2;

	switch (id) {
//	case fourcc_code('N','V','1','2'):
//		break;
	case fourcc_code('Y','V','1','2'):
	case fourcc_code('I','4','2','0'):
		nplanes = 3;
		srcpitch1 = ALIGN(src_w, 4);
		srcpitch2 = ALIGN(src_w / 2, 4);
		bufpitch1 = ALIGN(width, 4);
		bufpitch2 = ALIGN(width / 2, 4);
		depth = 8;
		src_h2 = src_h / 2;
		src_w2 = src_w / 2;
		break;
	case fourcc_code('U','Y','V','Y'):
	case fourcc_code('Y','U','Y','V'):
	case fourcc_code('Y','U','Y','2'):
		nplanes = 1;
		srcpitch1 = src_w * 2;
		bufpitch1 = width * 2;
		depth = 16;
		srcpitch2 = bufpitch2 = src_h2 = src_w2 = 0;
		break;
	default:
		ERROR_MSG("unexpected format: %08x (%4.4s)", id, (char *)&id);
		return BadMatch;
	}

	if (pPriv->format != id) {
		freebufs(pScreen, pPriv);
	}

	pPriv->format = id;
	pPriv->nplanes = nplanes;

	pPriv->pSrcPix[0] = setupplane(pScreen, pPriv->pSrcPix[0],
			src_w, src_h, depth, srcpitch1, bufpitch1, &buf);

	for (i = 1; i < pPriv->nplanes; i++) {
		pPriv->pSrcPix[i] = setupplane(pScreen, pPriv->pSrcPix[i],
				src_w2, src_h2, depth, srcpitch2, bufpitch2, &buf);
	}

	/* note: OMAPVidCopyArea() handles the composite-clip, so we can
	 * ignore clipBoxes
	 */
	return OMAPVidCopyArea(&pPriv->pSrcPix[0]->drawable, &srcb,
			NULL, NULL, pDstDraw, &dstb,
			OMAPVideoPutTextureImage, pPriv, clipBoxes);
}

/**
 * QueryImageAttributes
 *
 * calculates
 * - size (memory required to store image),
 * - pitches,
 * - offsets
 * of image
 * depending on colorspace (id) and dimensions (w,h) of image
 * values of
 * - w,
 * - h
 * may be adjusted as needed
 *
 * @param pScrn unused
 * @param id colorspace of image
 * @param w pointer to width of image
 * @param h pointer to height of image
 * @param pitches pitches[i] = length of a scanline in plane[i]
 * @param offsets offsets[i] = offset of plane i from the beginning of the image
 * @return size of the memory required for the XvImage queried
 */
static int
OMAPVideoQueryImageAttributes(ScrnInfoPtr pScrn, int id,
		unsigned short *w, unsigned short *h,
		int *pitches, int *offsets)
{
	int size, tmp;

	if (*w > IMAGE_MAX_W)
		*w = IMAGE_MAX_W;
	if (*h > IMAGE_MAX_H)
		*h = IMAGE_MAX_H;

	*w = (*w + 1) & ~1; // width rounded up to an even number
	if (offsets)
		offsets[0] = 0;

	switch (id) {
	case fourcc_code('Y','V','1','2'):
	case fourcc_code('I','4','2','0'):
		*h = (*h + 1) & ~1; // height rounded up to an even number
		size = (*w + 3) & ~3; // width rounded up to a multiple of 4
		if (pitches)
			pitches[0] = size; // width rounded up to a multiple of 4
		size *= *h;
		if (offsets)
			offsets[1] = size; // number of pixels in "rounded up" image
		tmp = ((*w >> 1) + 3) & ~3; // width/2 rounded up to a multiple of 4
		if (pitches)
			pitches[1] = pitches[2] = tmp; // width/2 rounded up to a multiple of 4
		tmp *= (*h >> 1); // 1/4*number of pixels in "rounded up" image
		size += tmp; // 5/4*number of pixels in "rounded up" image
		if (offsets)
			offsets[2] = size; // 5/4*number of pixels in "rounded up" image
		size += tmp; // = 3/2*number of pixels in "rounded up" image
		break;
	case fourcc_code('U','Y','V','Y'):
	case fourcc_code('Y','U','Y','2'):
		size = *w << 1; // 2*width
		if (pitches)
			pitches[0] = size; // 2*width
		size *= *h; // 2*width*height
		break;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Unknown colorspace: %x\n", id);
		*w = *h = size = 0;
		break;
	}

	return size;
}

static XF86VideoAdaptorPtr
OMAPVideoSetupTexturedVideo(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	XF86VideoAdaptorPtr adapt;
	OMAPPortPrivPtr pPriv;
	int i, nformats, nsupported;
	static unsigned int formats[MAX_FORMATS];

	if (!has_video(pOMAP)) {
		return NULL;
	}

	if (!(adapt = calloc(1, sizeof(XF86VideoAdaptorRec) +
			sizeof(OMAPPortPrivRec) +
			(sizeof(DevUnion) * NUM_TEXTURE_PORTS)))) {
		return NULL;
	}


	adapt->type			= XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags		= 0;
	adapt->name			= (char *)"OMAP Textured Video";
	adapt->nEncodings	= ARRAY_SIZE(OMAPVideoEncoding);
	adapt->pEncodings	= OMAPVideoEncoding;
	adapt->nFormats		= ARRAY_SIZE(OMAPVideoFormats);
	adapt->pFormats		= OMAPVideoFormats;
	adapt->nPorts		= NUM_TEXTURE_PORTS;
	adapt->pPortPrivates	= (DevUnion*)(&adapt[1]);

	pPriv = (OMAPPortPrivPtr)(&adapt->pPortPrivates[NUM_TEXTURE_PORTS]);
	for(i = 0; i < NUM_TEXTURE_PORTS; i++)
		adapt->pPortPrivates[i].ptr = (pointer)(pPriv);

	adapt->nAttributes		= ARRAY_SIZE(OMAPVideoTexturedAttributes);
	adapt->pAttributes		= OMAPVideoTexturedAttributes;

	nformats = pOMAP->pOMAPEXA->GetFormats(formats);
	nsupported = 0;
	for (i = 0; i < nformats; i++) {
		switch (formats[i]) {
//		case fourcc_code('N','V','1','2'):
//			break;
		case fourcc_code('Y','V','1','2'):
			OMAPVideoTexturedImages[nsupported++] =
					(XF86ImageRec)XVIMAGE_YV12;
			break;
		case fourcc_code('I','4','2','0'):
			OMAPVideoTexturedImages[nsupported++] =
					(XF86ImageRec)XVIMAGE_I420;
			break;
		case fourcc_code('U','Y','V','Y'):
			OMAPVideoTexturedImages[nsupported++] =
					(XF86ImageRec)XVIMAGE_UYVY;
			break;
		case fourcc_code('Y','U','Y','V'):
		case fourcc_code('Y','U','Y','2'):
			OMAPVideoTexturedImages[nsupported++] =
					(XF86ImageRec)XVIMAGE_YUY2;
			break;
		default:
			/* ignore unsupported formats */
			break;
		}
	}

	adapt->nImages			= nsupported;
	adapt->pImages			= OMAPVideoTexturedImages;

	adapt->PutVideo			= NULL;
	adapt->PutStill			= NULL;
	adapt->GetVideo			= NULL;
	adapt->GetStill			= NULL;
	adapt->StopVideo		= OMAPVideoStopVideo;
	adapt->SetPortAttribute	= OMAPVideoSetPortAttribute;
	adapt->GetPortAttribute	= OMAPVideoGetPortAttribute;
	adapt->QueryBestSize	= OMAPVideoQueryBestSize;
	adapt->PutImage			= OMAPVideoPutImage;
	adapt->QueryImageAttributes	= OMAPVideoQueryImageAttributes;

	return adapt;
}

/**
 * If EXA implementation supports GetFormats() and PutTextureImage() we can
 * use that to implement XV.  There is a copy involve because we need to
 * copy the buffer to a texture (TODO possibly we can support wrapping
 * external buffers, but current EXA submodule API doesn't give us a way to
 * do that).  So for optimal path from hw decoders to display, dri2video
 * should be used.  But this at least helps out legacy apps.
 */
Bool
OMAPVideoScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	XF86VideoAdaptorPtr textureAdaptor =
			OMAPVideoSetupTexturedVideo(pScreen);

	if (textureAdaptor) {
		XF86VideoAdaptorPtr *adaptors, *newAdaptors;
		int n = xf86XVListGenericAdaptors(pScrn, &adaptors);
		newAdaptors = calloc(n + 1, sizeof(XF86VideoAdaptorPtr *));
		memcpy(newAdaptors, adaptors, n * sizeof(XF86VideoAdaptorPtr *));
		pOMAP->textureAdaptor = textureAdaptor;
		newAdaptors[n] = textureAdaptor;
		xf86XVScreenInit(pScreen, newAdaptors, n + 1);
		free(newAdaptors);
		return TRUE;
	}

	return FALSE;
}

void
OMAPVideoCloseScreen(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	if (pOMAP->textureAdaptor) {
		OMAPPortPrivPtr pPriv = (OMAPPortPrivPtr)
				pOMAP->textureAdaptor->pPortPrivates[0].ptr;
		freebufs(pScreen, pPriv);
	}
}
