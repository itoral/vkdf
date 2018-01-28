BUILT_SOURCES = \
    shadow-map.vert.spv \
    ssao.deferred.vert.spv \
    ssao.deferred.frag.spv \
    ssao-blur.deferred.frag.spv


CLEANFILES = \
    $(BUILT_SOURCES)

shadow-map.vert.spv: shadow-map.vert
	$(top_srcdir)/$(GLSLANG) -V shadow-map.vert -o shadow-map.vert.spv

ssao.deferred.vert.spv: ssao.deferred.vert
	$(top_srcdir)/$(GLSLANG) -V ssao.deferred.vert -o ssao.deferred.vert.spv

ssao.deferred.frag.spv: ssao.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V ssao.deferred.frag -o ssao.deferred.frag.spv

ssao-blur.deferred.frag.spv: ssao-blur.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V ssao-blur.deferred.frag -o ssao-blur.deferred.frag.spv

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
