m4_define([prj_name],    [vkdf])
m4_define([prj_version], [0.0.1-0])

AC_INIT([prj_name], [prj_version])

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AC_CONFIG_SRCDIR([framework])

AM_CONFIG_HEADER(framework/config.h)

AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AM_PROG_CC_C_O

AC_HEADER_STDC

AM_MAINTAINER_MODE

CFLAGS="$CFLAGS -Wall -Werror -Wmissing-prototypes -Wmissing-declarations -std=c99"

# ----------------------------------------------------------
# DEBUG SUPPORT
# ----------------------------------------------------------

AC_ARG_ENABLE(debug,
        [  --enable-debug include debug symbols],,
        enable_debug=no)
if test "x$enable_debug" = "xyes"; then
        AC_MSG_WARN("Configuring with debugging options enabled!")
	CFLAGS="$CFLAGS -g3 -O0"
	CXXFLAGS="$CXXFLAGS -g3 -O0"
	VKDF_DEFINES="-DENABLE_DEBUG=1"
	AC_SUBST(VKDF_DEFINES)
else
	VKDF_DEFINES="-DENABLE_DEBUG=0 -DNDEBUG"
	AC_SUBST(VKDF_DEFINES)
fi

AM_CONDITIONAL([DEBUG], [test "x$enable_debug" = "xyes"])

# ----------------------------------------------------------
# DEPENDENCIES
# ----------------------------------------------------------

# Choose platform: SDL2 or GLFW3
PKG_CHECK_MODULES(SDL2, sdl2, [VKDF_PLATFORM_SDL=yes], [VKDF_PLATFORM_SDL=no])
PKG_CHECK_MODULES(GLFW3, glfw3, [VKDF_PLATFORM_GLFW=yes], [VKDF_PLATFORM_GLFW=no])

AC_ARG_ENABLE(platform,
   [AS_HELP_STRING([--enable-plaform=sdl2|glfw3], [Select SDL2 or GLFW3 platform])],
   [with_platform=$enableval], [with_platform=auto])

if test "$with_platform" = "sdl2"; then
   VKDF_PLATFORM_GLFW="no"
   if test "x$VKDF_PLATFORM_SDL" == "xno"; then
      AC_MSG_ERROR([SDL2 platform not available])
   fi
fi

if test "$with_platform" = "glfw3"; then
   VKDF_PLATFORM_SDL="no"
   if test "x$VKDF_PLATFORM_GLFW" == "xno"; then
      AC_MSG_ERROR([GLFW3 platform not available])
   fi
fi

if test "$VKDF_PLATFORM_SDL" == "yes"; then
   AC_DEFINE(VKDF_PLATFORM_SDL, [1], [Have SDL2])
   PLATFORM_CFLAGS="$SDL2_CFLAGS"
   PLATFORM_LIBS="$SDL2_LIBS"
elif test "$VKDF_PLATFORM_GLFW" == "yes"; then
   AC_DEFINE(VKDF_PLATFORM_GLFW, [1], [Have GLFW3])
   PLATFORM_CFLAGS="$GLFW3_CFLAGS"
   PLATFORM_LIBS="$GLFW3_LIBS"
else
   AC_MSG_ERROR([VKDF requires SDL2 or GLFW3])
fi

AC_SUBST(PLATFORM_CFLAGS)
AC_SUBST(PLATFORM_LIBS)

# Framework dependencies
PKG_CHECK_MODULES(VKDF_DEPS, glib-2.0 vulkan glm assimp SDL2_image)
AC_SUBST(VKDF_DEPS_CFLAGS)
AC_SUBST(VKDF_DEPS_LIBS)

# Demo dependencies
PKG_CHECK_MODULES(DEMO_DEPS, glib-2.0 vulkan glm)
AC_SUBST(DEMO_DEPS_CFLAGS)
AC_SUBST(DEMO_DEPS_LIBS)

# GLSLlang binary
GLSLANG="external/glslang/glslangValidator"
AC_SUBST(GLSLANG)

# ----------------------------------------------------------
# OUTPUT
# ----------------------------------------------------------

AC_CONFIG_FILES([
   Makefile
   data/Makefile
   data/spirv/Makefile
   framework/Makefile
   demos/Makefile
   demos/triangle/Makefile
   demos/offscreen/Makefile
   demos/texture/Makefile
   demos/depth/Makefile
   demos/mesh/Makefile
   demos/model/Makefile
   demos/pbr/Makefile
   demos/shadow/Makefile
   demos/scene/Makefile
   demos/scenelight/Makefile
   demos/sponza/Makefile
])

AC_OUTPUT

# Configuration summary
echo ""
echo "Configuration:"
echo "---------------------------------"
echo ""

if test "$VKDF_PLATFORM_SDL" = "yes"; then
   echo "Platform:    sdl2"
else
   echo "Platform:    glfw3"
fi

if test "$enable_debug" = "yes"; then
   echo "   Debug:    enabled"
else
   echo "   Debug:    disabled"
fi

echo ""
