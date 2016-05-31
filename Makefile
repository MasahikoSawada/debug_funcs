# debug_funcs

MODULE_big = debug_funcs
OBJS = debug_funcs.o 

EXTENSION = debug_funcs
DATA = debug_funcs--1.0.sql
PGFILEDESC = "debug_funcs"

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
