SUBDIRS = visualc macosx

noinst_LIBRARIES = libplatform.a
libplatform_a_SOURCES = sdl_platform.cpp
libplatform_a_CPPFLAGS = -I$(top_srcdir)/include $(SDL_CFLAGS)

EXTRA_DIST = sdl-win32.diff
