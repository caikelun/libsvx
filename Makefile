ifdef BUILD
ifeq ("$(origin BUILD)","command line")
export BUILD
endif
endif

ifdef COVERAGE
ifeq ("$(origin COVERAGE)","command line")
export COVERAGE
endif
endif

ifdef GPROF
ifeq ("$(origin GPROF)","command line")
export GPROF
endif
endif

.PHONY: all strip lib lib-strip test test-strip benchmarks benchmarks-strip doc coverageclean clean distclean

all: lib test benchmarks

strip: lib-strip test-strip benchmarks-strip

lib:
	@make -C ./src lib

lib-strip:
	@make -C ./src lib-strip

test: lib
	@make -C ./test

test-strip: lib-strip
	@make -C ./test strip

benchmarks: lib
	@make -C ./benchmarks/httpserver

benchmarks-strip: lib-strip
	@make -C ./benchmarks/httpserver strip

doc:
	rm -fr ./doc/html/ ./doc/*.db
	doxygen ./doc/Doxyfile

coverageclean:
	@make -C ./src                   coverageclean
	@make -C ./test                  coverageclean
	@make -C ./benchmarks/httpserver coverageclean

clean:
	@make -C ./test                  clean
	@make -C ./benchmarks/httpserver clean
	@make -C ./src                   clean
	rm -fr ./doc/html/ ./doc/*.db

distclean:
	@make -C ./test                  distclean
	@make -C ./benchmarks/httpserver distclean
	@make -C ./src                   distclean
	rm -fr ./doc/html/ ./doc/*.db
