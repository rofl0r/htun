# -------------------------------------------------------------------------
# Makefile - htun build system
# Copyright (C) 2002 Moshe Jacobson <moshe@runslinux.net>, 
#                    Ola Nordström <ola@triblock.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
# -------------------------------------------------------------------------

# $Id: Makefile,v 2.16 2002/08/11 15:57:07 jehsom Exp $


CFLAGS = -I../include -I. -O -W -Wall -g -D_REENTRANT #-pg -a
LDFLAGS = -lfl -lpthread # -flex for linux, solaris ?
LEX_CFLAGS = -I../include -I. -g -D_REENTRANT #-pg -a

# in Linux, LFLAGS is empty. In Solaris, LFLAGS = -lnsl -lsocket
#LFLAGS = -lnsl -lsocket

VPATH	= .:../include
CC      := gcc
LEX     = flex
YACC    = yacc
INCLUDE := $(wildcard ../include/*.h)
SRC     = common.c server.c client.c queue.c tpool.c log.c main.c tun.c \
			http.c util.c clidata.c iprange.c srvproto2.c srvproto1.c
OBJS    := $(SRC:.c=.o)
CONFSRC = y.tab.c lex.yy.c
CONFOBS := $(CONFSRC:.c=.o)
BIN     = htund

#all: $(BIN) tags
all: $(BIN)

debug: CFLAGS += -D_DEBUG
debug: all

$(BIN): $(CONFOBS) $(OBJS)
	$(CC) $(CFLAGS) $(IFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): $(INCLUDE)

$(CONFOBS): $(CONFSRC)
	$(CC) $(LEX_CFLAGS) -c $(@:.o=.c)

lex.yy.c: parse.l
	$(LEX) $^

y.tab.c: gram.y
	$(YACC) -d -v $^

tags: $(subst %.o,%.c,$(CONFOBS)) $(SRC) $(INCLUDE)
	@-ctags --c-types=+px $(shell $(CC) -M $(CFLAGS) $(filter %.c,$^) | \
		sed "s/^[^ ]*://; s/\\\\//g;")

.PHONY: clean
clean:
	rm -f $(BIN) $(OBJS) $(CONFOBS) y.tab.[ch] lex.yy.c y.output

.PHONY: distclean
distclean: clean
	rm -f tags
