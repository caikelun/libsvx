#
# This source code has been dedicated to the public domain by the authors.
# Anyone is free to copy, modify, publish, use, compile, sell, or distribute
# this source code, either in source code form or as a compiled binary, 
# for any purpose, commercial or non-commercial, and by any means.
#

build              := r
prof               := n
cover              := n
trapv              := n
asan               := n
tsan               := n

PATH_BIN           ?= .
PATH_SRC           ?= .
PATH_INC           ?= .
PATH_LIB           +=

FILE_BASE          ?= $(shell basename `pwd`)
FILE_EXE           ?= $(PATH_BIN)/$(FILE_BASE)
FILE_STLIB         ?= $(PATH_BIN)/$(FILE_BASE).a
FILE_DYLIB         ?= $(PATH_BIN)/$(FILE_BASE).so
FILE_AUTO          ?=
FILE_SRC_C         ?= $(wildcard $(foreach i,$(PATH_SRC),$(i)/*.c))
FILE_SRC_CPP       ?= $(wildcard $(foreach i,$(PATH_SRC),$(i)/*.cpp))
FILE_OBJ           ?= $(patsubst %.c,%.o,$(FILE_SRC_C)) $(patsubst %.cpp,%.o,$(FILE_SRC_CPP))
FILE_DEP           ?= $(patsubst %.c,%.d,$(FILE_SRC_C)) $(patsubst %.cpp,%.d,$(FILE_SRC_CPP))
FILE_PROF          ?= $(PATH_BIN)/gmon.out
FILE_GCNO          ?= $(patsubst %.c,%.gcno,$(FILE_SRC_C)) $(patsubst %.cpp,%.gcno,$(FILE_SRC_CPP))
FILE_GCDA          ?= $(patsubst %.c,%.gcda,$(FILE_SRC_C)) $(patsubst %.cpp,%.gcda,$(FILE_SRC_CPP))
FILE_GCOV          ?= $(patsubst %.c,%.c.gcov,$(FILE_SRC_C)) $(patsubst %.cpp,%.cpp.gcov,$(FILE_SRC_CPP))
FILE_LIB           +=
FILE_LIB_DEP       +=
FILE_CLEAN         += $(FILE_OBJ) $(FILE_DEP) $(FILE_EXE) $(FILE_STLIB) $(FILE_DYLIB) $(FILE_PROF) $(FILE_GCNO) $(FILE_GCDA) $(FILE_GCOV)
FILE_DISTCLEAN     += $(FILE_CLEAN) $(FILE_AUTO)

TOOL_CROSS_COMPILE ?=
TOOL_CC            ?= $(TOOL_CROSS_COMPILE)gcc
TOOL_AR            ?= $(TOOL_CROSS_COMPILE)ar
TOOL_RANLIB        ?= $(TOOL_CROSS_COMPILE)ranlib
TOOL_STRIP         ?= $(TOOL_CROSS_COMPILE)strip

OPTION_MACRO       +=
OPTION_INC         += $(foreach i,$(PATH_INC),-I$(i))
OPTION_LIB         += $(foreach i,$(PATH_LIB),-L$(i))

ifeq ($(build),d)
    OPTION_FLAG       += -O0 -g3
    ifeq ($(cover),y)
        OPTION_FLAG   += --coverage
        OPTION_LDFLAG += --coverage
    endif
    ifeq ($(trapv),y)
        OPTION_FLAG   += -ftrapv
    endif
    ifeq ($(asan),y)
        OPTION_FLAG   += -fsanitize=address -fsanitize=leak -fsanitize=undefined
        OPTION_LDFLAG += -fsanitize=address -fsanitize=leak -fsanitize=undefined
    endif
    ifeq ($(tsan),y)
        OPTION_FLAG   += -fsanitize=thread -fPIE
        OPTION_LDFLAG += -fsanitize=thread -fPIE -pie
    endif
else
    OPTION_FLAG       += -O3 -fvisibility=hidden
    ifeq ($(prof),y)
        OPTION_FLAG   += -pg
        OPTION_LDFLAG += -pg
    endif
endif
OPTION_FLAG        += -fPIC -Wall -Wextra -Werror
OPTION_CFLAG       += $(OPTION_FLAG) -std=c11
OPTION_CPPFLAG     += $(OPTION_FLAG) -std=c++11
OPTION_LDFLAG      += -fPIC

%.o: %.c
	$(TOOL_CC) $(OPTION_CFLAG) $(OPTION_MACRO) $(OPTION_INC) -c -o $@ $<
%.o: %.cpp
	$(TOOL_CC) $(OPTION_CPPFLAG) $(OPTION_MACRO) $(OPTION_INC) -c -o $@ $<
%.d: %.c
	@set -e; $(TOOL_CC) $(OPTION_CFLAG) $(OPTION_MACRO) $(OPTION_INC) -MM -MQ $*.o -MQ $*.d -MF $@ $<
%.d: %.cpp
	@set -e; $(TOOL_CC) $(OPTION_CPPFLAG) $(OPTION_MACRO) $(OPTION_INC) -MM -MQ $*.o -MQ $*.d -MF $@ $<

.PHONY: all lib stlib dylib clean distclean

ifeq ($(build),d)
all: $(FILE_EXE)
stlib: $(FILE_STLIB)
dylib: $(FILE_DYLIB)
else
all: $(FILE_EXE)
	$(TOOL_STRIP) --strip-all $(FILE_EXE)
stlib: $(FILE_STLIB)
	$(TOOL_STRIP) --strip-debug --strip-unneeded $(FILE_STLIB)
dylib: $(FILE_DYLIB)
	$(TOOL_STRIP) --strip-all $(FILE_DYLIB)
endif

lib: stlib dylib

clean:
	rm -f $(FILE_CLEAN)

distclean:
	rm -f $(FILE_DISTCLEAN)

$(FILE_EXE): $(FILE_OBJ) $(FILE_LIB_DEP)
	$(TOOL_CC) $(OPTION_LDFLAG) $(OPTION_LIB) -o $(FILE_EXE) $(FILE_OBJ) $(FILE_LIB)

$(FILE_STLIB): $(FILE_OBJ)
	$(TOOL_AR) cru $(FILE_STLIB) $(FILE_OBJ)
	$(TOOL_RANLIB) $(FILE_STLIB)

$(FILE_DYLIB): $(FILE_OBJ) $(FILE_LIB_DEP)
	$(TOOL_CC) -shared $(OPTION_LIB) -o $(FILE_DYLIB) $(FILE_OBJ) $(FILE_LIB)

-include $(FILE_DEP)
