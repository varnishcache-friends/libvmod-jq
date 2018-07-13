
TESTS_ENVIRONMENT = PATH=$(VARNISH_BINDIR):$(VARNISH_SBINDIR):$$PATH

vmoddir = $(VMOD_DIR)
vmod_LTLIBRARIES = libvmod_jq.la

libvmod_jq_la_CFLAGS = $(VMOD_INCLUDES)
libvmod_jq_la_LDFLAGS = -module -export-dynamic -avoid-version -shared \
	$(LIBS)

libvmod_jq_la_SOURCES = \
	vmod_jq.c
nodist_libvmod_jq_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

dist_man_MANS = vmod_jq.3

VTC_LOG_COMPILER = $(VARNISHTEST) -v \
	-Dvmod_topbuild=$(abs_top_builddir) -Dvmod_topsrc=$(abs_top_srcdir)
TEST_EXTENSIONS = .vtc
TESTS = @VMOD_TESTS@

$(libvmod_jq_la_OBJECTS): vcc_if.h

vcc_if.h vmod_jq.man.rst vmod_jq.rst: vcc_if.c

vcc_if.c: $(VMODTOOL) $(top_srcdir)/src/vmod_jq.vcc
	$(PYTHON) $(VMODTOOL) $(top_srcdir)/src/vmod_jq.vcc

vmod_jq.3: vmod_jq.man.rst
	$(RST2MAN) $? $@

EXTRA_DIST = vmod_jq.vcc \
	$(TESTS)

CLEANFILES = $(builddir)/vcc_if.c \
	$(builddir)/vcc_if.h \
	$(builddir)/vmod_jq.man.rst \
	$(builddir)/vmod_jq.rst \
	$(builddir)/$(dist_man_MANS)

if USE_LCOV
check-local:
	$(LCOV) -c -d .libs -o .libs/$(PACKAGE).info
	$(GENHTML) -o lcov .libs/$(PACKAGE).info

clean-local:
	rm -rf lcov
endif
