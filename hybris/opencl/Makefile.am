lib_LTLIBRARIES = \
	libOpenCL$(CL_LIB_SUFFIX).la

libOpenCL__CL_LIB_SUFFIX__la_SOURCES = \
	opencl.c

if WANT_CLICD
cl_icddir = $(sysconfdir)/OpenCL/vendors
cl_icd_DATA = libhybris.icd
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = OpenCL.pc

libOpenCL__CL_LIB_SUFFIX__la_CFLAGS = -I$(top_srcdir)/include $(ANDROID_HEADERS_CFLAGS) -DPKGLIBDIR="\"$(pkglibdir)/\"" -I$(top_srcdir)/common
if WANT_MESA
libOpenCL__CL_LIB_SUFFIX__la_CFLAGS += -DLIBHYBRIS_WANTS_MESA_X11_HEADERS
endif
if WANT_TRACE
libOpenCL__CL_LIB_SUFFIX__la_CFLAGS += -DDEBUG
endif
if WANT_DEBUG
libOpenCL__CL_LIB_SUFFIX__la_CFLAGS += -ggdb -O0
endif
if WANT_CLICD
libOpenCL__CL_LIB_SUFFIX__la_CFLAGS += -DLIBHYBRIS_WANTS_CLICD
endif

libOpenCL__CL_LIB_SUFFIX__la_CXXFLAGS = -I$(top_srcdir)/include $(ANDROID_HEADERS_CFLAGS) -I$(top_srcdir)/common -DPKGLIBDIR="\"$(pkglibdir)/\""
if WANT_MESA
libOpenCL__CL_LIB_SUFFIX__la_CXXFLAGS += -DLIBHYBRIS_WANTS_MESA_X11_HEADERS
endif
if WANT_TRACE
libOpenCL__CL_LIB_SUFFIX__la_CXXFLAGS += -DDEBUG
endif
if WANT_DEBUG
libOpenCL__CL_LIB_SUFFIX__la_CXXFLAGS += -ggdb -O0
endif
if WANT_CLICD
libOpenCL__CL_LIB_SUFFIX__la_CXXFLAGS += -DLIBHYBRIS_WANTS_CLICD
endif
libOpenCL__CL_LIB_SUFFIX__la_LDFLAGS = \
	-ldl \
	$(top_builddir)/common/libhybris-common.la \
	-version-info "1":"0":"0"
