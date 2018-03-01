BUILT_SOURCES = \
    shadow-map.vert.spv \
    ssao.deferred.vert.spv \
    ssao.deferred.frag \
    ssao.deferred.frag.spv \
    ssao-blur.deferred.vert.spv \
    ssao-blur.deferred.frag \
    ssao-blur.deferred.frag.spv \
    ssr-blur.vert.spv \
    ssr-blur.frag.spv \
    ssr.vert.spv \
    ssr.frag.spv \
    ssr-blend.vert.spv \
    ssr-blend.frag.spv \
    tone-map.vert.spv \
    tone-map.frag.spv \
    fxaa.vert.spv \
    fxaa.frag.spv


CLEANFILES = \
    $(BUILT_SOURCES)

# Shadow map
shadow-map.vert.spv: shadow-map.vert
	$(top_srcdir)/$(GLSLANG) -V shadow-map.vert -o shadow-map.vert.spv


# SSAO
ssao.deferred.vert.spv: ssao.deferred.vert
	$(top_srcdir)/$(GLSLANG) -V ssao.deferred.vert -o ssao.deferred.vert.spv

ssao.deferred.frag: ssao.deferred.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py ssao.deferred.frag.input ssao.deferred.frag

ssao.deferred.frag.spv: ssao.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V ssao.deferred.frag -o ssao.deferred.frag.spv

ssao-blur.deferred.vert.spv: ssao-blur.deferred.vert
	$(top_srcdir)/$(GLSLANG) -V ssao-blur.deferred.vert -o ssao-blur.deferred.vert.spv

ssao-blur.deferred.frag: ssao-blur.deferred.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py ssao-blur.deferred.frag.input ssao-blur.deferred.frag

ssao-blur.deferred.frag.spv: ssao-blur.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V ssao-blur.deferred.frag -o ssao-blur.deferred.frag.spv


# SSR
ssr.vert.spv: ssr.vert
	$(top_srcdir)/$(GLSLANG) -V ssr.vert -o ssr.vert.spv

ssr.frag.spv: ssr.frag
	$(top_srcdir)/$(GLSLANG) -V ssr.frag -o ssr.frag.spv

ssr-blur.vert.spv: ssr-blur.vert
	$(top_srcdir)/$(GLSLANG) -V ssr-blur.vert -o ssr-blur.vert.spv

ssr-blur.frag.spv: ssr-blur.frag
	$(top_srcdir)/$(GLSLANG) -V ssr-blur.frag -o ssr-blur.frag.spv

ssr-blend.vert.spv: ssr-blend.vert
	$(top_srcdir)/$(GLSLANG) -V ssr-blend.vert -o ssr-blend.vert.spv

ssr-blend.frag.spv: ssr-blend.frag
	$(top_srcdir)/$(GLSLANG) -V ssr-blend.frag -o ssr-blend.frag.spv


# HDR Tone mapping
tone-map.vert.spv: tone-map.vert
	$(top_srcdir)/$(GLSLANG) -V tone-map.vert -o tone-map.vert.spv

tone-map.frag.spv: tone-map.frag
	$(top_srcdir)/$(GLSLANG) -V tone-map.frag -o tone-map.frag.spv


# FXAA
fxaa.vert.spv: fxaa.vert
	$(top_srcdir)/$(GLSLANG) -V fxaa.vert -o fxaa.vert.spv

fxaa.frag.spv: fxaa.frag
	$(top_srcdir)/$(GLSLANG) -V fxaa.frag -o fxaa.frag.spv


MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
