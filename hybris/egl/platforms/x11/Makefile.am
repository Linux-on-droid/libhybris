pkglib_LTLIBRARIES = eglplatform_x11.la

eglplatform_x11_la_SOURCES = eglplatform_x11.cpp x11_window.cpp xcb_drihybris.c
eglplatform_x11_la_CXXFLAGS = \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/egl \
	-I$(top_srcdir)/egl/platforms/common \
	$(ANDROID_HEADERS_CFLAGS) \
	$(WAYLAND_CLIENT_CFLAGS)

if WANT_DEBUG
eglplatform_x11_la_CXXFLAGS += -I$(top_srcdir)/common
endif

if WANT_TRACE
eglplatform_x11_la_CXXFLAGS += -DDEBUG
endif

if WANT_DEBUG
eglplatform_x11_la_CXXFLAGS += -ggdb -O0
endif

if !WANT_WL_SERVERSIDE_BUFFERS
eglplatform_x11_la_CXXFLAGS += -DHYBRIS_NO_SERVER_SIDE_BUFFERS
endif



eglplatform_x11_la_LDFLAGS = \
	-avoid-version -module -shared -export-dynamic \
	$(top_builddir)/egl/platforms/common/libhybris-eglplatformcommon.la \
	$(top_builddir)/gralloc/libgralloc.la \
	-lXext -lxcb -lX11-xcb -lxcb-present

if HAS_ANDROID_4_2_0
eglplatform_x11_la_LDFLAGS += $(top_builddir)/libsync/libsync.la
endif

if HAS_ANDROID_5_0_0
eglplatform_x11_la_LDFLAGS += $(top_builddir)/libsync/libsync.la
endif

