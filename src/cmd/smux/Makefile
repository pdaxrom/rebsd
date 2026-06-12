SMUX_SUBDIRS ?= retro linux

all:
	for i in ${SMUX_SUBDIRS}; do ${MAKE} -C $$i || exit $$?; done

install:
	${MAKE} -C retro install

clean:
	${MAKE} -C retro clean
	${MAKE} -C linux clean
