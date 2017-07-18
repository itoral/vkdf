bin_PROGRAMS = ssbo

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Triangle
# ------------------------------

GLSL_GENERATED_FILES = \
    shader_1.vert \
    shader_2.vert \
    shader_3.vert \
    shader_4.vert

SPIRV_GENERATED_FILES = \
    shader_1.vert.spv \
    shader_2.vert.spv \
    shader_3.vert.spv \
    shader_4.vert.spv

BUILT_SOURCES = \
	$(GLSL_GENERATED_FILES) \
	$(SPIRV_GENERATED_FILES)

CLEANFILES = \
	$(BUILT_SOURCES)

$(GLSL_GENERATED_FILES): shader.vert.mako
	python gen_vertex_shader.py

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
