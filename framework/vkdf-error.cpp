#include "vkdf.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void
vkdf_error(const char *msg, ...)
{
   char text[1024];
   va_list ap;

   if (!msg)
      return;

   va_start(ap, msg);
   vsprintf(text, msg, ap);
   fprintf(stderr, "VKDF ERROR: %s\n", text);
   va_end(ap);
}

void
vkdf_fatal(const char *msg, ...)
{
   char text[1024];
   va_list ap;

   if (!msg)
      return;

   va_start(ap, msg);
   vsprintf(text, msg, ap);
   fprintf(stderr, "VKDF FATAL: %s\n", text);
   va_end(ap);

   exit(-1);
}

void
vkdf_info(const char *msg, ...)
{
   char text[1024];
   va_list ap;

   if (!msg)
      return;

   va_start(ap, msg);
   vsprintf(text, msg, ap);
   fprintf(stdout, "VKDF INFO: %s", text);
   va_end(ap);
}
