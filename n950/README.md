# Gecko ESR 153.6 with EmbedLite for the Nokia N950 (Maemo Leste, armhf)

The engine of a slim browser for the N950: Gecko with EmbedLite, under a
native Qt 5 UI, no Firefox front-end.

## Source

`gecko-dev/` is not the submodule here: it is the Firefox 153.6.0esr source
release (`firefox-153.6.0esr.source.tar.xz` from archive.mozilla.org,
checked against its `SHA512SUMS`) with `n950/gecko-patches/*.patch` applied
in order (`git am`). They are:

- Sailfish OS's EmbedLite patch stack and this fork's ESR 153 work
  (`fork-esr153` of github smatkovi/gecko-dev-esr153), rebased from 153.2.0
  onto 153.6.0 (conflicts in storage/mozStorageConnection.cpp and
  config/milestone.txt resolved),
- the N950 build: Qt linked as Core and Gui only, `--disable-qt-geolocation`,
  Qt's headers as system headers (clang), no unixproxy on Qt,
- WebRender on OpenGL ES 2.0 (PowerVR SGX530), in progress: ES 2.0 context
  detection, GLSL ES 1.00 shaders (all 49 GLES variants compile in an ES 2.0
  context), wrench `--renderer es2` and `compile_shaders`.

`embedding/embedlite` here carries the EmbedLite side (SetDPI through the
APZ input bridge for 153.6).

## Build

With the scripts of github smatkovi/n950-maemo-leste:

    SRC=<this checkout>/gecko-dev SYSROOT=<Leste armhf root> \
        bash browser/embedlite-n950/build.sh build
