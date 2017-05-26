bin_PROGRAMS = ssbo

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Triangle
# ------------------------------

BUILT_SOURCES = \
    shader.vert.spv

CLEANFILES = \
    $(BUILT_SOURCES)

shader.vert.spv: shader.vert
	$(top_srcdir)/$(GLSLANG) -V shader.vert -o shader.vert.spv

ssbo_SOURCES = \
    main.cpp

ssbo_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

ssbo_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lvulkan \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
