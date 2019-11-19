bin_PROGRAMS = cpu-particle-source

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# Triangle
# ------------------------------

BUILT_SOURCES = \
    particle.vert.spv \
    particle.frag.spv

CLEANFILES = \
    $(BUILT_SOURCES)

particle.vert.spv: particle.vert
	$(top_srcdir)/$(GLSLANG) -V particle.vert -o particle.vert.spv

particle.frag.spv: particle.frag
	$(top_srcdir)/$(GLSLANG) -V particle.frag -o particle.frag.spv

cpu_particle_source_SOURCES = \
    main.cpp

cpu_particle_source_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

cpu_particle_source_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
