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
	VKDF_DEFINES="-DENABLE_DEBUG=0"
	AC_SUBST(VKDF_DEFINES)
fi

AM_CONDITIONAL([DEBUG], [test "x$enable_debug" = "xyes"])

# ----------------------------------------------------------
# DEPENDENCIES
# ----------------------------------------------------------

PKG_CHECK_MODULES(VKDF_DEPS, glib-2.0 vulkan glm glfw3 assimp)
AC_SUBST(VKDF_DEPS_CFLAGS)
AC_SUBST(VKDF_DEPS_LIBS)

PKG_CHECK_MODULES(DEMO_DEPS, glib-2.0 vulkan glfw3)
AC_SUBST(DEMO_DEPS_CFLAGS)
AC_SUBST(DEMO_DEPS_LIBS)

GLSLANG="external/glslang/glslangValidator"
AC_SUBST(GLSLANG)

# ----------------------------------------------------------
# OUTPUT
# ----------------------------------------------------------

AC_CONFIG_FILES([
   Makefile
   framework/Makefile
   demos/Makefile
   demos/triangle/Makefile
   demos/offscreen/Makefile
   demos/texture/Makefile
   demos/depth/Makefile
   demos/mesh/Makefile
   demos/model/Makefile
   demos/light/Makefile
   demos/shadow/Makefile
])

AC_OUTPUT

