bin_PROGRAMS = sponza

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Sponza
# ------------------------------

BUILT_SOURCES = \
    obj.depthprepass.vert.spv \
    obj.depthprepass.opacity.vert.spv \
    obj.depthprepass.opacity.frag.spv \
    obj.vert \
    obj.vert.spv \
    obj.frag \
    obj.frag.spv \
    obj_opacity.frag \
    obj_opacity.frag.spv \
    obj.deferred.vert \
    obj.deferred.vert.spv \
    obj.deferred.frag \
    obj.deferred.frag.spv \
    obj_opacity.deferred.frag \
    obj_opacity.deferred.frag.spv \
    gbuffer-merge.vert.spv \
    gbuffer-merge.frag \
    gbuffer-merge.frag.spv \
    debug-tile.vert.spv \
    debug-tile.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

# Depth pre-pass
obj.depthprepass.vert.spv: obj.depthprepass.vert
	$(top_srcdir)/$(GLSLANG) -V obj.depthprepass.vert -o obj.depthprepass.vert.spv

obj.depthprepass.opacity.vert.spv: obj.depthprepass.opacity.vert
	$(top_srcdir)/$(GLSLANG) -V obj.depthprepass.opacity.vert -o obj.depthprepass.opacity.vert.spv

obj.depthprepass.opacity.frag.spv: obj.depthprepass.opacity.frag
	$(top_srcdir)/$(GLSLANG) -V obj.depthprepass.opacity.frag -o obj.depthprepass.opacity.frag.spv


# Forward shading
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


# Deferred shading
obj.deferred.vert: obj.deferred.vert.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj.deferred.vert.input obj.deferred.vert

obj.deferred.vert.spv: obj.deferred.vert
	$(top_srcdir)/$(GLSLANG) -V obj.deferred.vert -o obj.deferred.vert.spv

obj.deferred.frag: obj.deferred.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj.deferred.frag.input obj.deferred.frag

obj.deferred.frag.spv: obj.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V obj.deferred.frag -o obj.deferred.frag.spv

obj_opacity.deferred.frag: obj_opacity.deferred.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py obj_opacity.deferred.frag.input obj_opacity.deferred.frag

obj_opacity.deferred.frag.spv: obj_opacity.deferred.frag
	$(top_srcdir)/$(GLSLANG) -V obj_opacity.deferred.frag -o obj_opacity.deferred.frag.spv

gbuffer-merge.vert.spv: gbuffer-merge.vert
	$(top_srcdir)/$(GLSLANG) -V gbuffer-merge.vert -o gbuffer-merge.vert.spv

gbuffer-merge.frag: gbuffer-merge.frag.input
	python $(top_srcdir)/scripts/fixup-glsl.py gbuffer-merge.frag.input gbuffer-merge.frag

gbuffer-merge.frag.spv: gbuffer-merge.frag
	$(top_srcdir)/$(GLSLANG) -V gbuffer-merge.frag -o gbuffer-merge.frag.spv


# Debug
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
