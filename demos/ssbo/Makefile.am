bin_PROGRAMS = ssbo

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Triangle
# ------------------------------

BUILT_SOURCES = \
    shader_1.vert \
    shader_2.vert \
    shader_3.vert \
    shader_4.vert \
    shader_1.vert.spv \
    shader_2.vert.spv \
    shader_3.vert.spv \
    shader_4.vert.spv

CLEANFILES = \
    $(BUILT_SOURCES)

shader_1.vert shader_2.vert shader_3.vert shader_4.vert: shader.vert.mako
	python gen_vertex_shader.py

shader_1.vert.spv: shader_1.vert
	$(top_srcdir)/$(GLSLANG) -V shader_1.vert -o shader_1.vert.spv

shader_2.vert.spv: shader_2.vert
	$(top_srcdir)/$(GLSLANG) -V shader_2.vert -o shader_2.vert.spv

shader_3.vert.spv: shader_3.vert
	$(top_srcdir)/$(GLSLANG) -V shader_3.vert -o shader_3.vert.spv

shader_4.vert.spv: shader_4.vert
	$(top_srcdir)/$(GLSLANG) -V shader_4.vert -o shader_4.vert.spv

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
