BUILT_SOURCES = \
    shadow-map.vert.spv \
    ssao.deferred.vert.spv \
    ssao.deferred.frag \
    ssao.deferred.frag.spv \
    ssao-blur.deferred.vert.spv \
    ssao-blur.deferred.frag.spv \
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

ssao-blur.deferred.frag.spv: ssao-blur.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V ssao-blur.deferred.frag -o ssao-blur.deferred.frag.spv


# FXAA
fxaa.vert.spv: fxaa.vert
	$(top_srcdir)/$(GLSLANG) -V fxaa.vert -o fxaa.vert.spv

fxaa.frag.spv: fxaa.frag
	$(top_srcdir)/$(GLSLANG) -V fxaa.frag -o fxaa.frag.spv


MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
