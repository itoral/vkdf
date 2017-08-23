BUILT_SOURCES = \
    shadow-map.vert.spv

CLEANFILES = \
    $(BUILT_SOURCES)

shadow-map.vert.spv: shadow-map.vert
	$(top_srcdir)/$(GLSLANG) -V shadow-map.vert -o shadow-map.vert.spv

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
