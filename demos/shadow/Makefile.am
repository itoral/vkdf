bin_PROGRAMS = shadow

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@ -I$(top_srcdir)/framework

BUILT_SOURCES = \
    shader.frag \
    shader.vert.spv \
    shader.frag.spv \
    shadow.vert.spv \
    ui-tile.vert.spv \
    ui-tile.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

shader.frag: shader.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py shader.frag.input shader.frag

shader.vert.spv: shader.vert
	$(top_srcdir)/$(GLSLANG) -V shader.vert -o shader.vert.spv

shader.frag.spv: shader.frag
	$(top_srcdir)/$(GLSLANG) -V shader.frag -o shader.frag.spv

shadow.vert.spv: shadow.vert
	$(top_srcdir)/$(GLSLANG) -V shadow.vert -o shadow.vert.spv

ui-tile.vert.spv: ui-tile.vert
	$(top_srcdir)/$(GLSLANG) -V ui-tile.vert -o ui-tile.vert.spv

ui-tile.frag.spv: ui-tile.frag
	$(top_srcdir)/$(GLSLANG) -V ui-tile.frag -o ui-tile.frag.spv

shadow_SOURCES = \
    main.cpp

shadow_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

shadow_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    @PLATFORM_LIBS@ \
    -lvulkan \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
