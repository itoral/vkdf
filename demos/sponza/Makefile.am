bin_PROGRAMS = sponza

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Sponza
# ------------------------------

BUILT_SOURCES = \
    obj.vert \
    obj.vert.spv \
    obj.frag \
    obj.frag.spv \
    obj_opacity.frag \
    obj_opacity.frag.spv \
    debug-tile.vert.spv \
    debug-tile.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

obj.vert: obj.vert.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj.vert.input obj.vert

obj.vert.spv: obj.vert
	$(top_srcdir)/$(GLSLANG) -V obj.vert -o obj.vert.spv

obj.frag: obj.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj.frag.input obj.frag

obj.frag.spv: obj.frag
	$(top_srcdir)/$(GLSLANG) -V obj.frag -o obj.frag.spv

obj_opacity.frag: obj_opacity.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj_opacity.frag.input obj_opacity.frag

obj_opacity.frag.spv: obj_opacity.frag
	$(top_srcdir)/$(GLSLANG) -V obj_opacity.frag -o obj_opacity.frag.spv

debug-tile.vert.spv: debug-tile.vert
	$(top_srcdir)/$(GLSLANG) -V debug-tile.vert -o debug-tile.vert.spv

debug-tile.frag.spv: debug-tile.frag
	$(top_srcdir)/$(GLSLANG) -V debug-tile.frag -o debug-tile.frag.spv

sponza_SOURCES = \
    main.cpp

sponza_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

sponza_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
