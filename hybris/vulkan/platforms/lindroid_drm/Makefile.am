pkglib_LTLIBRARIES = vulkanplatform_lindroid-drm.la

WAYLAND_DMABUF_XML = \
	$(top_srcdir)/platforms/wayland/protocols/linux-dmabuf-unstable-v1.xml

BUILT_SOURCES = \
	linux-dmabuf-unstable-v1-client-protocol.h \
	linux-dmabuf-unstable-v1-protocol.c

EXTRA_DIST = $(WAYLAND_DMABUF_XML)

CLEANFILES = $(BUILT_SOURCES)

linux-dmabuf-unstable-v1-client-protocol.h: $(WAYLAND_DMABUF_XML)
	$(WAYLAND_SCANNER) client-header $< $@

linux-dmabuf-unstable-v1-protocol.c: $(WAYLAND_DMABUF_XML)
	$(WAYLAND_SCANNER) private-code $< $@

vulkanplatform_lindroid_drm_la_SOURCES = \
	vulkanplatform_lindroid_drm.cpp \
	$(top_srcdir)/platforms/wayland/wayland_window_common.cpp \
	$(top_srcdir)/vulkan/platforms/wayland/wayland_window.cpp

nodist_vulkanplatform_lindroid_drm_la_SOURCES = \
	linux-dmabuf-unstable-v1-protocol.c

vulkanplatform_lindroid_drm_la_CXXFLAGS = \
	-DWANT_LINDROID_DRM \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/platforms/common \
	-I$(top_srcdir)/platforms/wayland \
	-I$(top_builddir)/platforms/common \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/vulkan \
	-I$(top_srcdir)/vulkan/platforms/common \
	-I$(top_srcdir)/vulkan/platforms/wayland \
	-I$(top_builddir)/vulkan/platforms/common \
	-I$(top_builddir) \
	-I$(builddir) \
	$(ANDROID_HEADERS_CFLAGS) \
	$(WAYLAND_CLIENT_CFLAGS) \
	$(WAYLAND_EGL_CFLAGS) \
	$(LIBDRM_CFLAGS) \
	$(GBM_CFLAGS)

if WANT_DEBUG
vulkanplatform_lindroid_drm_la_CXXFLAGS += -I$(top_srcdir)/common
endif

if WANT_TRACE
vulkanplatform_lindroid_drm_la_CXXFLAGS += -DDEBUG
endif

if WANT_DEBUG
vulkanplatform_lindroid_drm_la_CXXFLAGS += -ggdb -O0
endif

if !WANT_WL_SERVERSIDE_BUFFERS
vulkanplatform_lindroid_drm_la_CXXFLAGS += -DHYBRIS_NO_SERVER_SIDE_BUFFERS
endif

vulkanplatform_lindroid_drm_la_LDFLAGS = \
	-avoid-version -module -shared -export-dynamic

vulkanplatform_lindroid_drm_la_LIBADD = \
	$(top_builddir)/platforms/common/libhybris-platformcommon.la \
	$(top_builddir)/vulkan/platforms/common/libhybris-vulkanplatformcommon.la \
	$(top_builddir)/gralloc/libgralloc.la \
	$(WAYLAND_CLIENT_LIBS) \
	$(WAYLAND_EGL_LIBS) \
	$(LIBDRM_LIBS) \
	$(GBM_LIBS)

if HAS_ANDROID_4_2_0
vulkanplatform_lindroid_drm_la_LIBADD += $(top_builddir)/libsync/libsync.la
endif

if HAS_ANDROID_5_0_0
vulkanplatform_lindroid_drm_la_LIBADD += $(top_builddir)/libsync/libsync.la
endif
