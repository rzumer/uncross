import vapoursynth as vs
core = vs.get_core()
#video = <insert your source filter here>

# Any pre-processing to video should be performed here

pre = video[0] + video

# super clip
super = core.mv.Super(video, hpad=0, vpad=0, pel=1, chroma=True, rfilter=0)

# mmap
anal = core.mv.Analyse(super=super, blksize=4, search=3, searchparam=4, chroma=False, truemotion=False)
mmask = core.mv.Mask(clip=video, vectors=anal, ml=2, time=0, thscd1=0, thscd2=255)
mmask = core.std.Binarize(mmask,threshold=255,v0=0,v1=255,planes=[0])
mmask = core.std.ShufflePlanes(mmask, [0,0,0], vs.YUV)
mmask = core.resize.Bilinear(mmask,format=vs.YUV420P8)

# mcmap
comp = core.mv.Compensate(clip=video, super=super, vectors=anal)#, thsad=16)
precomp = comp[0] + comp
mcmask = core.std.MakeDiff(video, comp, 0)
above = core.std.Binarize(mcmask,threshold=127+16,v0=0,v1=255,planes=[0])
below = core.std.Binarize(mcmask,threshold=127-16,v0=255,v1=0,planes=[0])
mcmask = core.std.Merge(above, below, .5)
mcmask = core.std.Binarize(mcmask,threshold=127,v0=0,v1=255,planes=[0])
mcmask = core.std.ShufflePlanes(mcmask, [0,0,0], vs.YUV)
mcmask = core.resize.Bilinear(mcmask,format=vs.YUV420P8)

# dcmap frame n
dcmap = core.dotdetect.Detect(video, 2)
dcmap = core.std.Merge(dcmap, mmask, .5)
dcmap = core.std.Binarize(dcmap,threshold=255,v0=0,v1=255,planes=[0])
dcmap = core.std.ShufflePlanes(dcmap, [0,0,0], vs.YUV)
dcmap = core.resize.Bilinear(dcmap,format=vs.YUV420P8)

dcmappre = dcmap[0] + dcmap

# rbmap
upscaled = core.resize.Bilinear(video,format=vs.YUV444P8)
upscaledmmap = core.resize.Bilinear(mmask,format=vs.YUV444P8)
rbmap = core.rainbowdetect.Detect(upscaled,10,5,5,20,20)
#rbmap = core.std.Merge(rbmap, upscaledmmap, .5)
rbmap = core.std.Binarize(rbmap,threshold=255,v0=0,v1=255,planes=[0])
rbmap = core.std.ShufflePlanes(rbmap, [0,0,0], vs.YUV)
rbmap = core.resize.Bilinear(rbmap,format=vs.YUV420P8)

# filtering (by merging masks where appropriate)
nommask = core.std.Levels(core.std.Invert(mmask),0,255,1,0,127)
#filtered = core.std.Merge(video,pre,0.5)
filtered = core.std.MaskedMerge(video, pre, nommask, [0,1,2], True)
tempmask = core.std.Merge(mmask,core.std.Invert(mcmask,[0,1,2]),.5)
tempmask = core.std.Binarize(tempmask,threshold=255,v0=0,v1=255,planes=[0])
mcmasknew = tempmask
dcmapboth = core.std.Merge(dcmap,dcmappre,.5)
dcmapboth = core.std.Binarize(dcmapboth,threshold=255,v0=0,v1=255,planes=[0])
#dcmapboth = core.std.ShufflePlanes(dcmapboth, [0,0,0], vs.YUV)
dcmapneither = core.std.Merge(core.std.Invert(dcmap),core.std.Invert(dcmappre),.5)
dcmapneither = core.std.Binarize(dcmapneither,threshold=255,v0=0,v1=255,planes=[0])
dcmapeither = core.std.Merge(dcmap,dcmappre,.5)
dcmapeither = core.std.Binarize(dcmapeither,threshold=127,v0=0,v1=255,planes=[0])
dcmapone = core.std.Merge(dcmapeither, core.std.Invert(dcmapboth), .5)
dcmapone = core.std.Binarize(dcmapone, threshold=255,v0=0,v1=255,planes=[0])
rbmaponly = core.std.Merge(rbmap,dcmapneither,.5)
rbmaponly = core.std.Binarize(rbmaponly,threshold=255,v0=0,v1=255,planes=[0])
rbordcmap = core.std.Merge(dcmapboth, rbmaponly, .5)
rbordcmap = core.std.Binarize(rbordcmap,threshold=127,v0=0,v1=255,planes=[0])
tempmask = core.std.Merge(tempmask,rbordcmap,.5)
tempmask = core.std.Binarize(tempmask,threshold=255,v0=0,v1=255,planes=[0])
tempmask = core.std.ShufflePlanes(tempmask, [0,0,0], vs.YUV)
tempmask = core.resize.Bilinear(tempmask,format=vs.YUV420P8)
tempmask = core.std.Levels(tempmask,0,255,1,0,127)

spacemask = core.std.ShufflePlanes(dcmapone, [0,0,0], vs.YUV)
spacemask = core.resize.Bilinear(spacemask,format=vs.YUV420P8)
spacemask = core.std.Levels(spacemask,0,255,1,0,127)

blurred = core.dotblur.Blur(upscaled)
blurred = core.resize.Bilinear(blurred,format=vs.YUV420P8)

filtered = core.std.MaskedMerge(filtered, comp, tempmask, [0,1,2], True)
filtered = core.std.MaskedMerge(filtered, blurred, spacemask, [0,1,2], True)

#int = core.std.Interleave(clips=[video, mmask, mcmask, tempmask])
int = core.std.Interleave([video,filtered])
int.set_output()
