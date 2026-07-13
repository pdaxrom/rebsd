all: test

compile:
	sh ./smoke-usb-desc.sh compile
	sh ./smoke-usb-core.sh compile
	sh ./smoke-ukbd.sh compile
	sh ./smoke-ohci.sh compile
	sh ./smoke-ci20-usb-hw.sh compile

test:
	sh ./smoke-usb-desc.sh test
	sh ./smoke-usb-core.sh test
	sh ./smoke-ukbd.sh test
	sh ./smoke-ohci.sh test
	sh ./smoke-ci20-usb-hw.sh test

.PHONY: all compile test
