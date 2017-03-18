#include <stdlib.h>
#include <VapourSynth.h>
#include <VSHelper.h>

typedef struct {
	VSNodeRef *node;
	const VSVideoInfo *vi;

	int threshY;
	int threshU1;
	int threshV1;
	int threshU2;
	int threshV2;
} VideoData;

// This function is called immediately after vsapi->createFilter(). This is the only place where the video
// properties may be set. In this case we simply use the same as the input clip. You may pass an array
// of VSVideoInfo if the filter has more than one output, like rgb+alpha as two separate clips.
static void VS_CC init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	VideoData *d = (VideoData *)* instanceData;
	vsapi->setVideoInfo(d->vi, 1, node);
}

// Read plane data into a matrix.
static int **readPlaneMatrix(const VSFrameRef *frame, int plane, const VSAPI *vsapi) {
	int height = vsapi->getFrameHeight(frame, plane);
	int width = vsapi->getFrameWidth(frame, plane);

	// Allocate destination matrix.
	int **planeData = malloc(height * sizeof *planeData);

	for (int i = 0; i < height; i++) {
		planeData[i] = malloc(width * sizeof *planeData[i]);
	}

	// Read the frame data into the matrix.
	const uint8_t *srcp = vsapi->getReadPtr(frame, plane);
	int stride = vsapi->getStride(frame, plane);

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			planeData[y][x] = srcp[x];
		}

		srcp += stride;
	}

	return planeData;
}

// Write plane data from a matrix to a frame.
static void writePlaneMatrix(VSFrameRef *frame, int plane, int **planeData, const VSAPI *vsapi) {
	int height = vsapi->getFrameHeight(frame, plane);
	int width = vsapi->getFrameWidth(frame, plane);

	uint8_t *dstp = vsapi->getWritePtr(frame, plane);
	int stride = vsapi->getStride(frame, plane);

	// Write frame data from the matrix.
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			dstp[x] = planeData[y][x];
		}

		dstp += stride;
	}
}

int **generateRainbowMap(const VSFrameRef *frame, const VSFrameRef *previous, VideoData *context, const VSAPI *vsapi) {
	int height = vsapi->getFrameHeight(frame, 0); // same for all planes with YUV444P8
	int width = vsapi->getFrameWidth(frame, 0); // same for all planes with YUV444P8

	// Allocate destination matrix.
	int **rbMap = malloc(height * sizeof *rbMap);

	for (int i = 0; i < height; i++) {
		rbMap[i] = malloc(width * sizeof *rbMap[i]);
	}

	// Read the frame data into the matrix.
	const uint8_t *srcpy = vsapi->getReadPtr(frame, 0); // y plane pointer
	const uint8_t *srcpu = vsapi->getReadPtr(frame, 1); // u plane pointer
	const uint8_t *srcpv = vsapi->getReadPtr(frame, 2); // v plane pointer
	const uint8_t *prepu = vsapi->getReadPtr(previous, 1); // u previous plane pointer
	const uint8_t *prepv = vsapi->getReadPtr(previous, 2); // v previous plane pointer

	int stride = vsapi->getStride(frame, 0); // same for all planes with YUV444P8

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			rbMap[y][x] = 0;

			int du = abs(srcpu[x] - prepu[x]);
			int dv = abs(srcpv[x] - prepv[x]);

			if (srcpy[x] > context->threshY
				&& (context->threshU1 < du && du > context->threshU2
				|| context->threshV1 < dv && dv > context->threshV2)) {
				rbMap[y][x] = 255;
			}
		}

		srcpy += stride;
		srcpu += stride;
		srcpv += stride;
		prepu += stride;
		prepv += stride;
	}

	return rbMap;
}

// This is the main function that gets called when a frame should be produced. It will, in most cases, get
// called several times to produce one frame. This state is being kept track of by the value of
// activationReason. The first call to produce a certain frame n is always arInitial. In this state
// you should request all the input frames you need. Always do it in ascending order to play nice with the
// upstream filters.
// Once all frames are ready, the filter will be called with arAllFramesReady. It is now time to
// do the actual processing.
static const VSFrameRef *VS_CC getFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	VideoData *d = (VideoData *)* instanceData;

	if (activationReason == arInitial) {
		// Request the source frames on the first call
		if (n > 0) {
			vsapi->requestFrameFilter(n - 1, d->node, frameCtx);
		}

		vsapi->requestFrameFilter(n, d->node, frameCtx);
	}
	else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

		// The reason we query this on a per frame basis is because we want our filter
		// to accept clips with varying dimensions. If we reject such content using d->vi
		// would be better.
		const VSFormat *fi = d->vi->format;
		int height = vsapi->getFrameHeight(src, 0);
		int width = vsapi->getFrameWidth(src, 0);

		// When creating a new frame for output it is VERY EXTREMELY SUPER IMPORTANT to
		// supply the "dominant" source frame to copy properties from. Frame props
		// are an essential part of the filter chain and you should NEVER break it.
		VSFrameRef *dst = vsapi->newVideoFrame(fi, width, height, src, core);

		if (n == 0) {
			return dst;
		}

		const VSFrameRef *pre = vsapi->getFrameFilter(n - 1, d->node, frameCtx);

		int **rbMap = generateRainbowMap(src, pre, d, vsapi);

		// write the DCMap in the Y plane
		writePlaneMatrix(dst, 0, rbMap, vsapi);

		free(rbMap);
		vsapi->freeFrame(pre);
		vsapi->freeFrame(src);
		return dst;
	}

	return 0;
}

// Free all allocated data on filter destruction
static void VS_CC freeResources(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	VideoData *d = (VideoData *)instanceData;
	vsapi->freeNode(d->node);
	free(d);
}

// This function is responsible for validating arguments and creating a new filter
static void VS_CC create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	VideoData d;
	VideoData *data;
	int err;

	// Get a clip reference from the input arguments. This must be freed later.
	d.node = vsapi->propGetNode(in, "clip", 0, 0);
	d.vi = vsapi->getVideoInfo(d.node);

	// In this first version we only want to handle 8bit integer formats. Note that
	// vi->format can be 0 if the input clip can change format midstream.
	if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample != 8) {
		vsapi->setError(out, "RainbowDetect: only constant format 8-bit integer input supported");
		vsapi->freeNode(d.node);
		return;
	}
	
	// If a property read fails for some reason (index out of bounds/wrong type)
	// then err will have flags set to indicate why and 0 will be returned. This
	// can be very useful to know when having optional arguments. Since we have
	// strict checking because of what we wrote in the argument string, the only
	// reason this could fail is when the value wasn't set by the user.
	// And when it's not set we want it to default to enabled.
	d.threshY = !!vsapi->propGetInt(in, "threshY", 0, &err);
	if (err)
		d.threshY = 10;

	d.threshU1 = !!vsapi->propGetInt(in, "threshU1", 0, &err);
	if (err)
		d.threshU1 = 5;

	d.threshV1 = !!vsapi->propGetInt(in, "threshV1", 0, &err);
	if (err)
		d.threshV1 = 5;

	d.threshU2 = !!vsapi->propGetInt(in, "threshU2", 0, &err);
	if (err)
		d.threshU2 = 20;

	d.threshV2 = !!vsapi->propGetInt(in, "threshV2", 0, &err);
	if (err)
		d.threshV2 = 20;

	if (d.threshY < 0 || d.threshU1 < 0 || d.threshU2 < 0 || d.threshV1 < 0 || d.threshV2 < 0) {
		vsapi->setError(out, "RainbowDetect: threshold must be a positive value");
		vsapi->freeNode(d.node);
		return;
	}

	if (d.threshU2 < d.threshU1 || d.threshV2 < d.threshV1) {
		vsapi->setError(out, "RainbowDetect: thresh2 must be greater than thresh1");
		vsapi->freeNode(d.node);
		return;
	}

	if (d.vi->format->id != pfYUV444P8) {
		vsapi->setError(out, "RainbowDetect: YUV444P8 input is required");
		vsapi->freeNode(d.node);
		return;
	}

	// I usually keep the filter data struct on the stack and don't allocate it
	// until all the input validation is done.
	data = malloc(sizeof(d));
	*data = d;

	// Creates a new filter and returns a reference to it. Always pass on the in and out
	// arguments or unexpected things may happen. The name should be something that's
	// easy to connect to the filter, like its function name.
	// The three function pointers handle initialization, frame processing and filter destruction.
	// The filtermode is very important to get right as it controls how threading of the filter
	// is handled. In general you should only use fmParallel whenever possible. This is if you
	// need to modify no shared data at all when the filter is running.
	// For more complicated filters, fmParallelRequests is usually easier to achieve as it can
	// be prefetched in parallel but the actual processing is serialized.
	// The others can be considered special cases where fmSerial is useful to source filters and
	// fmUnordered is useful when a filter's state may change even when deciding which frames to
	// prefetch (such as a cache filter).
	// If your filter is really fast (such as a filter that only resorts frames) you should set the
	// nfNoCache flag to make the caching work smoother.
	vsapi->createFilter(in, out, "RainbowDetect", init, getFrame, freeResources, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

// This is the entry point that is called when a plugin is loaded. You are only supposed
// to call the two provided functions here.
// configFunc sets the id, namespace, and long name of the plugin (the last 3 arguments
// never need to be changed for a normal plugin).
//
// id: Needs to be a "reverse" url and unique among all plugins.
//   It is inspired by how android packages identify themselves.
//   If you don't own a domain then make one up that's related
//   to the plugin name.
//
// namespace: Should only use [a-z_] and not be too long.
//
// full name: Any name that describes the plugin nicely.
//
// registerFunc is called once for each function you want to register. Function names
// should be PascalCase. The argument string has this format:
// name:type; or name:type:flag1:flag2....;
// All argument name should be lowercase and only use [a-z_].
// The valid types are int,float,data,clip,frame,func. [] can be appended to allow arrays
// of type to be passed (numbers:int[])
// The available flags are opt, to make an argument optional, empty, which controls whether
// or not empty arrays are accepted

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("github.com.rzumer.rainbowdetect", "rainbowdetect", "Rainbow Detect", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("Detect", "clip:clip;threshY:int:opt;threshU1:int:opt;threshV1:int:opt;threshU2:int:opt;threshV2:int:opt;", create, 0, plugin);
}
