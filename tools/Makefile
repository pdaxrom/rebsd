SUBDIR		= elf2aout fsutil kconfig

ifneq ($(shell pkg-config --exists libusb-1.0 2>/dev/null && echo yes),)
SUBDIR		+= n64usbnet
endif

all install depend: ${SUBDIR}
		-for i in ${SUBDIR}; do ${MAKE} -C $$i ${MFLAGS} DESTDIR=${DESTDIR} $@; done

.PHONY: all

clean:
		rm -f *~
		for i in ${SUBDIR}; do ${MAKE} -C $$i ${MFLAGS} clean; done
