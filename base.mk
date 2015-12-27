#
# This source code has been dedicated to the public domain by the authors.
# Anyone is free to copy, modify, publish, use, compile, sell, or distribute
# this source code, either in source code form or as a compiled binary, 
# for any purpose, commercial or non-commercial, and by any means.
#

BUILD              := debug
COVERAGE           := no
GPROF              := no

PATH_BIN           ?= .
PATH_SRC           ?= .
PATH_INC           ?= .
PATH_LIB           += # ./my_libs

FILE_BASE          ?= $(shell basename `pwd`)
FILE_EXE           ?= $(PATH_BIN)/$(FILE_BASE)
FILE_STLIB         ?= $(PATH_BIN)/$(FILE_BASE).a
FILE_DYLIB         ?= $(PATH_BIN)/$(FILE_BASE).so
FILE_AUTO          ?= # ./my_auto_config.h
FILE_SRC_C         ?= $(wildcard $(foreach i,$(PATH_SRC),$(i)/*.c))
FILE_SRC_CPP       ?= $(wildcard $(foreach i,$(PATH_SRC),$(i)/*.cpp))
FILE_OBJ           ?= $(patsubst %.c,%.o,$(FILE_SRC_C)) $(patsubst %.cpp,%.o,$(FILE_SRC_CPP))
FILE_DEP           ?= $(patsubst %.c,%.d,$(FILE_SRC_C)) $(patsubst %.cpp,%.d,$(FILE_SRC_CPP))
FILE_GCNO          ?= $(patsubst %.c,%.gcno,$(FILE_SRC_C)) $(patsubst %.cpp,%.gcno,$(FILE_SRC_CPP))
FILE_GCDA          ?= $(patsubst %.c,%.gcda,$(FILE_SRC_C)) $(patsubst %.cpp,%.gcda,$(FILE_SRC_CPP))
FILE_GCOV          ?= $(patsubst %.c,%.c.gcov,$(FILE_SRC_C)) $(patsubst %.cpp,%.cpp.gcov,$(FILE_SRC_CPP))
FILE_COVERAGE      ?= $(FILE_GCDA) $(FILE_GCOV)
FILE_GPROF         ?= $(PATH_BIN)/gmon.out
FILE_LIB           += # -labc ./my_other_libs/libxyz.a
FILE_LIB_DEP       += # ./my_other_libs/libxyz.a
FILE_CLEAN         += $(FILE_OBJ) $(FILE_DEP) $(FILE_EXE) $(FILE_STLIB) $(FILE_DYLIB) $(FILE_GCNO) $(FILE_COVERAGE) $(FILE_GPROF)
FILE_DISTCLEAN     += $(FILE_CLEAN) $(FILE_AUTO)

TOOL_CROSS_COMPILE ?=
TOOL_CC            ?= $(TOOL_CROSS_COMPILE)gcc
TOOL_AR            ?= $(TOOL_CROSS_COMPILE)ar
TOOL_RANLIB        ?= $(TOOL_CROSS_COMPILE)ranlib
TOOL_STRIP         ?= $(TOOL_CROSS_COMPILE)strip

OPTION_FLAG        += -std=c11
ifeq ($(BUILD),debug)
OPTION_FLAG        += -O0 -g3
else
OPTION_FLAG        += -O3 -g3
endif

ifeq ($(COVERAGE),yes)
OPTION_FLAG        += --coverage
endif

ifeq ($(GPROF),yes)
OPTION_FLAG        += -pg
endif

OPTION_FLAG        += -fPIC -Wall -Werror
OPTION_MACRO       += # -DMY_MACRO -DNDEBUG
OPTION_INC         += $(foreach i,$(PATH_INC),-I$(i))
OPTION_LIB         += $(foreach i,$(PATH_LIB),-L$(i))

%.o: %.c
	$(TOOL_CC) $(OPTION_FLAG) $(OPTION_MACRO) $(OPTION_INC) -c -o $@ $<
%.o: %.cpp
	$(TOOL_CC) $(OPTION_FLAG) $(OPTION_MACRO) $(OPTION_INC) -c -o $@ $<
%.d: %.c
	@set -e; $(TOOL_CC) $(OPTION_FLAG) $(OPTION_MACRO) $(OPTION_INC) -MM -MQ $*.o -MQ $*.d -MF $@ $<
%.d: %.cpp
	@set -e; $(TOOL_CC) $(OPTION_FLAG) $(OPTION_MACRO) $(OPTION_INC) -MM -MQ $*.o -MQ $*.d -MF $@ $<

.PHONY: all strip lib lib-strip stlib stlib-strip dylib dylib-strip coverageclean clean distclean

all: $(FILE_EXE)

strip: $(FILE_EXE)
	$(TOOL_STRIP) --strip-all $(FILE_EXE)

lib: stlib dylib

lib-strip: stlib-strip dylib-strip

stlib: $(FILE_STLIB)

stlib-strip: $(FILE_STLIB)
	$(TOOL_STRIP) --strip-debug --strip-unneeded $(FILE_STLIB)

dylib: $(FILE_DYLIB)

dylib-strip: $(FILE_DYLIB)
	$(TOOL_STRIP) --strip-all $(FILE_DYLIB)

coverageclean:
	rm -f $(FILE_COVERAGE)

clean:
	rm -f $(FILE_CLEAN)

distclean:
	rm -f $(FILE_DISTCLEAN)

$(FILE_EXE): $(FILE_OBJ) $(FILE_LIB_DEP)
	$(TOOL_CC) $(OPTION_FLAG) $(OPTION_LIB) -o $(FILE_EXE) $(FILE_OBJ) $(FILE_LIB)

$(FILE_STLIB): $(FILE_OBJ)
	$(TOOL_AR) cru $(FILE_STLIB) $(FILE_OBJ)
	$(TOOL_RANLIB) $(FILE_STLIB)

$(FILE_DYLIB): $(FILE_OBJ) $(FILE_LIB_DEP)
	$(TOOL_CC) -shared $(OPTION_FLAG) $(OPTION_LIB) -o $(FILE_DYLIB) $(FILE_OBJ) $(FILE_LIB)

-include $(FILE_DEP)
