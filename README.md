# uncross
A set of Vapoursynth plugins and script based on the 2007 paper "Reduction of Dot Crawl and Rainbow Artifacts in the NTSC Video" by Ji Won Lee et al., which outlines a technique for temporal and spatial filtering of these artifacts.

Motion estimation and compensation make use of [`vapoursynth-mvtools`](https://github.com/dubhater/vapoursynth-mvtools).

Compile `dotdetect`, `rainbowdetect` and `dotblur` separately, and process a clip with `script.vpy` to try it out.

This method introduces significant blocking and undesirable blending artifacts and is not recommended for regular use.
