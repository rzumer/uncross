#include <stdlib.h>
#include <VapourSynth.h>
#include <VSHelper.h>

typedef struct {
	VSNodeRef *node;
	const VSVideoInfo *vi;

	int compensate;
} MotionData;

// This function is called immediately after vsapi->createFilter(). This is the only place where the video
// properties may be set. In this case we simply use the same as the input clip. You may pass an array
// of VSVideoInfo if the filter has more than one output, like rgb+alpha as two separate clips.
static void VS_CC init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	MotionData *d = (MotionData *)* instanceData;
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

// This is the main function that gets called when a frame should be produced. It will, in most cases, get
// called several times to produce one frame. This state is being kept track of by the value of
// activationReason. The first call to produce a certain frame n is always arInitial. In this state
// you should request all the input frames you need. Always do it in ascending order to play nice with the
// upstream filters.
// Once all frames are ready, the filter will be called with arAllFramesReady. It is now time to
// do the actual processing.
static const VSFrameRef *VS_CC getFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	MotionData *d = (MotionData *)* instanceData;

	if (activationReason == arInitial) {
		// Request the source frame on the first call
		vsapi->requestFrameFilter(n, d->node, frameCtx);
	}
	else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

		return src;
	}

	return 0;
}

// Free all allocated data on filter destruction
static void VS_CC freeResources(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	MotionData *d = (MotionData *)instanceData;
	vsapi->freeNode(d->node);
	free(d);
}

// This function is responsible for validating arguments and creating a new filter
static void VS_CC estimateCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	MotionData d;
	MotionData *data;
	int err;

	// Get a clip reference from the input arguments. This must be freed later.
	d.node = vsapi->propGetNode(in, "clip", 0, 0);
	d.vi = vsapi->getVideoInfo(d.node);

	// In this first version we only want to handle 8bit integer formats. Note that
	// vi->format can be 0 if the input clip can change format midstream.
	if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample != 8) {
		vsapi->setError(out, "MotionDetect: only constant format 8-bit integer input supported");
		vsapi->freeNode(d.node);
		return;
	}

	if (d.vi->format->id != pfYUV444P8) {
		vsapi->setError(out, "MotionDetect: YUV444P8 input is required");
		vsapi->freeNode(d.node);
		return;
	}

	// I usually keep the filter data struct on the stack and don't allocate it
	// until all the input validation is done.
	data = malloc(sizeof(d));
	*data = d;

	data->compensate = 0;

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
	vsapi->createFilter(in, out, "MotionEstimate", init, getFrame, freeResources, fmParallel, 0, data, core);
}

// This function is responsible for validating arguments and creating a new filter
static void VS_CC compensateCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	MotionData d;
	MotionData *data;
	int err;

	// Get a clip reference from the input arguments. This must be freed later.
	d.node = vsapi->propGetNode(in, "clip", 0, 0);
	d.vi = vsapi->getVideoInfo(d.node);

	// In this first version we only want to handle 8bit integer formats. Note that
	// vi->format can be 0 if the input clip can change format midstream.
	if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample != 8) {
		vsapi->setError(out, "MotionDetect: only constant format 8-bit integer input supported");
		vsapi->freeNode(d.node);
		return;
	}

	if (d.vi->format->id != pfYUV444P8) {
		vsapi->setError(out, "MotionDetect: YUV444P8 input is required");
		vsapi->freeNode(d.node);
		return;
	}

	// I usually keep the filter data struct on the stack and don't allocate it
	// until all the input validation is done.
	data = malloc(sizeof(d));
	*data = d;

	data->compensate = 1;

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
	vsapi->createFilter(in, out, "MotionCompensate", init, getFrame, freeResources, fmParallel, 0, data, core);
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
	configFunc("github.com.rzumer.motiondetect", "motiondetect", "MotionDetect", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("Estimate", "clip:clip;", estimateCreate, 0, plugin);
	registerFunc("Compensate", "clip:clip;", compensateCreate, 0, plugin);
}
