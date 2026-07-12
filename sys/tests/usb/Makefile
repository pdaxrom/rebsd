all: test

compile:
	sh ./smoke-usb-desc.sh compile
	sh ./smoke-usb-core.sh compile

test:
	sh ./smoke-usb-desc.sh test
	sh ./smoke-usb-core.sh test

.PHONY: all compile test
