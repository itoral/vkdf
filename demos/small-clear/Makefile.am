bin_PROGRAMS = small_clear

AM_CPPFLAGS = @DEMO_DEPS_CFLAGS@

# ------------------------------
# small
# ------------------------------

small_clear_SOURCES = \
    main.cpp

small_clear_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@

small_clear_LDADD = \
    $(abs_top_builddir)/framework/.libs/libvkdf.so \
    @DEMO_DEPS_LIBS@ \
    -lm

# -----------------------------

MAINTAINERCLEANFILES = \
	*.in \
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
