bin_PROGRAMS = mesh

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Mesh
# ------------------------------

BUILT_SOURCES = \
    shader.vert.spv \
    shader.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

shader.vert.spv: shader.vert
	$(top_srcdir)/$(GLSLANG) -V shader.vert -o shader.vert.spv

shader.frag.spv: shader.frag
	$(top_srcdir)/$(GLSLANG) -V shader.frag -o shader.frag.spv

mesh_SOURCES = \
    main.cpp

mesh_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

mesh_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lvulkan \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
