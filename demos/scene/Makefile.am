bin_PROGRAMS = scene

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Scene
# ------------------------------

BUILT_SOURCES = \
    scene.vert.spv \
    scene.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

scene.vert.spv: scene.vert
	$(top_srcdir)/$(GLSLANG) -V scene.vert -o scene.vert.spv

scene.frag.spv: scene.frag
	$(top_srcdir)/$(GLSLANG) -V scene.frag -o scene.frag.spv

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
