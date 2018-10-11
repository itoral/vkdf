bin_PROGRAMS = pbr

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# PBR
# ------------------------------

BUILT_SOURCES = \
    shader.vert.spv \
    shader.frag \
    shader.frag.spv \
    shader-ibl.frag \
    shader-ibl.frag.spv \
    shader-tex.vert.spv \
    shader-tex.frag \
    shader-tex.frag.spv \
    shader-ibl-tex.frag \
    shader-ibl-tex.frag.spv \
    shader-ibl-cubemap.vert.spv \
    shader-ibl-cubemap.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

shader.vert.spv: shader.vert
	$(top_srcdir)/$(GLSLANG) -V shader.vert -o shader.vert.spv

shader.frag: shader.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py shader.frag.input shader.frag

shader.frag.spv: shader.frag
	$(top_srcdir)/$(GLSLANG) -V shader.frag -o shader.frag.spv

shader-ibl.frag: shader-ibl.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py shader-ibl.frag.input shader-ibl.frag

shader-ibl.frag.spv: shader-ibl.frag
	$(top_srcdir)/$(GLSLANG) -V shader-ibl.frag -o shader-ibl.frag.spv

shader-tex.vert.spv: shader-tex.vert
	$(top_srcdir)/$(GLSLANG) -V shader-tex.vert -o shader-tex.vert.spv

shader-tex.frag: shader-tex.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py shader-tex.frag.input shader-tex.frag

shader-tex.frag.spv: shader-tex.frag
	$(top_srcdir)/$(GLSLANG) -V shader-tex.frag -o shader-tex.frag.spv

shader-ibl-tex.frag: shader-ibl-tex.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py shader-ibl-tex.frag.input shader-ibl-tex.frag

shader-ibl-tex.frag.spv: shader-ibl-tex.frag
	$(top_srcdir)/$(GLSLANG) -V shader-ibl-tex.frag -o shader-ibl-tex.frag.spv

shader-ibl-cubemap.vert.spv: shader-ibl-cubemap.vert
	$(top_srcdir)/$(GLSLANG) -V shader-ibl-cubemap.vert -o shader-ibl-cubemap.vert.spv

shader-ibl-cubemap.frag.spv: shader-ibl-cubemap.frag
	$(top_srcdir)/$(GLSLANG) -V shader-ibl-cubemap.frag -o shader-ibl-cubemap.frag.spv

pbr_SOURCES = \
    main.cpp

pbr_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

pbr_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
