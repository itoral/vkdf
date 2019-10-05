AM_CPPFLAGS = @PLATFORM_CFLAGS@ @VKDF_DEPS_CFLAGS@

lib_LTLIBRARIES = libvkdf.la

libvkdf_la_SOURCES = \
    vkdf.hpp \
    vkdf-util.hpp vkdf-util.cpp \
    vkdf-thread-pool.hpp vkdf-thread-pool.cpp \
    vkdf-box.hpp vkdf-box.cpp \
    vkdf-frustum.hpp vkdf-frustum.cpp \
    vkdf-plane.hpp vkdf-plane.cpp \
    vkdf-error.hpp vkdf-error.cpp \
    vkdf-platform.hpp vkdf-platform.cpp \
    vkdf-init.hpp vkdf-init-priv.hpp vkdf-init.cpp \
    vkdf-event-loop.hpp vkdf-event-loop.cpp \
    vkdf-cmd-buffer.hpp vkdf-cmd-buffer.cpp \
    vkdf-buffer.hpp vkdf-buffer.cpp \
    vkdf-memory.hpp vkdf-memory.cpp \
    vkdf-shader.hpp vkdf-shader.cpp \
    vkdf-pipeline.hpp vkdf-pipeline.cpp \
    vkdf-framebuffer.hpp vkdf-framebuffer.cpp \
    vkdf-renderpass.hpp vkdf-renderpass.cpp \
    vkdf-descriptor.hpp vkdf-descriptor.cpp \
    vkdf-image.hpp vkdf-image.cpp \
    vkdf-sampler.hpp vkdf-sampler.cpp \
    vkdf-barrier.hpp vkdf-barrier.cpp \
    vkdf-semaphore.hpp vkdf-semaphore.cpp \
    vkdf-mesh.hpp vkdf-mesh.cpp \
    vkdf-model.hpp vkdf-model.cpp \
    vkdf-object.hpp vkdf-object.cpp \
    vkdf-light.hpp vkdf-light.cpp \
    vkdf-camera.hpp vkdf-camera.cpp \
    vkdf-ssao.hpp vkdf-ssao.cpp \
    vkdf-scene.hpp vkdf-scene.cpp \
    vkdf-terrain.hpp vkdf-terrain.cpp \
    vkdf-skybox.hpp vkdf-skybox.cpp

libvkdf_la_CXXFLAGS = \
    -DPREFIX=$(prefix) \
    -D_GNU_SOURCE \
    @VKDF_DEFINES@ \
    @DEFS@ \
    -DVKDF_DATA_DIR=\"$(abs_top_builddir)/data/\"

libvkdf_la_LIBADD = \
    @PLATFORM_LIBS@ \
    @VKDF_DEPS_LIBS@ \
    -lvulkan \
    -lm

MAINTAINERCLEANFILES = \
        *.in \
        *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)

