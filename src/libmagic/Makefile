TOPSRC		= $(shell cd ../..; pwd)
include $(TOPSRC)/target.mk

FILE_HOST	?= file
HOSTCC		?= cc

CFLAGS		+= -Os -Wall -Werror -DHAVE_CONFIG_H \
		   -DMAGIC='"/usr/share/misc/magic.mgc"' -I.

SRCS		= buffer.c magic.c apprentice.c softmagic.c ascmagic.c \
		  encoding.c compress.c is_csv.c is_json.c is_simh.c is_tar.c \
		  readelf.c print.c fsmagic.c funcs.c apptype.c der.c cdf.c \
		  cdf_time.c readcdf.c
OBJS		= $(SRCS:.c=.o)

all:		../libmagic.a

../libmagic.a:	$(OBJS)
		$(AR) cru $@ $(OBJS)
		$(RANLIB) $@

install:

regen-magic:
		@$(FILE_HOST) -v 2>&1 | grep '5\.47' >/dev/null || { \
		    echo "FILE_HOST must be file 5.47" >&2; exit 1; \
		}
		$(FILE_HOST) -C -m magic
		$(HOSTCC) -DHAVE_CONFIG_H -I. -o magic-endian-host magic-endian.c
		./magic-endian-host magic.mgc magic.mgc.little little
		./magic-endian-host magic.mgc magic.mgc.big big
		rm -f magic.mgc magic-endian-host

clean:
		rm -f $(OBJS) ../libmagic.a magic-endian-host a.out core *~
