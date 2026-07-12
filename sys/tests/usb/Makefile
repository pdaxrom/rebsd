all: test

compile:
	sh ./smoke-usb-desc.sh compile
	sh ./smoke-usb-core.sh compile
	sh ./smoke-ohci.sh compile

test:
	sh ./smoke-usb-desc.sh test
	sh ./smoke-usb-core.sh test
	sh ./smoke-ohci.sh test

.PHONY: all compile test
