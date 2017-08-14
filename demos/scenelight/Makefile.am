bin_PROGRAMS = scene

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Scene
# ------------------------------

BUILT_SOURCES = \
    obj.vert.spv \
    obj.frag \
    obj.frag.spv \
    floor.vert.spv \
    floor.frag \
    floor.frag.spv \
    debug-tile.vert.spv \
    debug-tile.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

obj.vert.spv: obj.vert
	$(top_srcdir)/$(GLSLANG) -V obj.vert -o obj.vert.spv

obj.frag: obj.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj.frag.input obj.frag

obj.frag.spv: obj.frag
	$(top_srcdir)/$(GLSLANG) -V obj.frag -o obj.frag.spv

floor.vert.spv: floor.vert
	$(top_srcdir)/$(GLSLANG) -V floor.vert -o floor.vert.spv

floor.frag: floor.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py floor.frag.input floor.frag

floor.frag.spv: floor.frag
	$(top_srcdir)/$(GLSLANG) -V floor.frag -o floor.frag.spv

debug-tile.vert.spv: debug-tile.vert
	$(top_srcdir)/$(GLSLANG) -V debug-tile.vert -o debug-tile.vert.spv

debug-tile.frag.spv: debug-tile.frag
	$(top_srcdir)/$(GLSLANG) -V debug-tile.frag -o debug-tile.frag.spv

scene_SOURCES = \
    main.cpp

scene_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

scene_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
