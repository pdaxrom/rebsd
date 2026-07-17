all: test

VM_TEST_SRCDIR = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

compile:
	sh $(VM_TEST_SRCDIR)/smoke-vm.sh compile

test:
	sh $(VM_TEST_SRCDIR)/smoke-vm.sh test

.PHONY: all compile test
