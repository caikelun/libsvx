ifdef build
ifeq ("$(origin build)","command line")
export build
endif
endif

ifdef prof
ifeq ("$(origin prof)","command line")
export prof
endif
endif

ifdef cover
ifeq ("$(origin cover)","command line")
export cover
endif
endif

ifdef trapv
ifeq ("$(origin trapv)","command line")
export trapv
endif
endif

ifdef asan
ifeq ("$(origin asan)","command line")
export asan
endif
endif

ifdef tsan
ifeq ("$(origin tsan)","command line")
export tsan
endif
endif

.PHONY: all lib test benchmarks clean distclean doc

all: lib test benchmarks

lib:
	@make -C ./src lib

test: lib
	@make -C ./test

benchmarks: lib
	@make -C ./benchmarks/httpserver

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

doc:
	rm -fr ./doc/html/ ./doc/*.db
	doxygen ./doc/Doxyfile
