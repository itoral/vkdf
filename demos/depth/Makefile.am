bin_PROGRAMS = depth

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Depth
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

depth_SOURCES = \
    main.cpp

depth_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

depth_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lvulkan \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
