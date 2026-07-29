all: test

compile:
	sh ./smoke-usb-desc.sh compile
	sh ./smoke-usb-core.sh compile
	sh ./smoke-usb-task.sh compile
	sh ./smoke-uhub.sh compile
	sh ./smoke-input.sh compile
	sh ./smoke-ukbd.sh compile
	sh ./smoke-ums.sh compile
	sh ./smoke-ohci.sh compile
	sh ./smoke-uhci.sh compile
	sh ./smoke-ehci.sh compile
	sh ./smoke-ci20-usb-hw.sh compile
	sh ./smoke-umass.sh compile

test:
	sh ./smoke-usb-desc.sh test
	sh ./smoke-usb-core.sh test
	sh ./smoke-usb-task.sh test
	sh ./smoke-uhub.sh test
	sh ./smoke-input.sh test
	sh ./smoke-ukbd.sh test
	sh ./smoke-ums.sh test
	sh ./smoke-ohci.sh test
	sh ./smoke-uhci.sh test
	sh ./smoke-ehci.sh test
	sh ./smoke-ci20-usb-hw.sh test
	sh ./smoke-umass.sh test

.PHONY: all compile test
