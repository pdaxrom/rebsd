# Source lookup for an out-of-tree build.  Keep this list input-only: a broad
# VPATH would let stale binaries and generated files in the source tree mask
# missing object-tree targets.

vpath %.c $(REBSD_SRCDIR)
vpath %.h $(REBSD_SRCDIR)
vpath %.S $(REBSD_SRCDIR)
vpath %.s $(REBSD_SRCDIR)
vpath %.y $(REBSD_SRCDIR)
vpath %.l $(REBSD_SRCDIR)
vpath %.sh $(REBSD_SRCDIR)
vpath %.inc $(REBSD_SRCDIR)
vpath %.def $(REBSD_SRCDIR)
vpath %.tbl $(REBSD_SRCDIR)
vpath %.awk $(REBSD_SRCDIR)
vpath %.sed $(REBSD_SRCDIR)
vpath %.kconf $(REBSD_SRCDIR)
vpath %.manifest $(REBSD_SRCDIR)
vpath %.conf $(REBSD_SRCDIR)
vpath %.dat $(REBSD_SRCDIR)
vpath %.small $(REBSD_SRCDIR)
vpath %.src $(REBSD_SRCDIR)
vpath %.1 $(REBSD_SRCDIR)
vpath %.2 $(REBSD_SRCDIR)
vpath %.3 $(REBSD_SRCDIR)
vpath %.4 $(REBSD_SRCDIR)
vpath %.5 $(REBSD_SRCDIR)
vpath %.6 $(REBSD_SRCDIR)
vpath %.7 $(REBSD_SRCDIR)
vpath %.8 $(REBSD_SRCDIR)

# A few historical generators use extensionless source inputs.
vpath Config $(REBSD_SRCDIR)
vpath tokenscript $(REBSD_SRCDIR)
