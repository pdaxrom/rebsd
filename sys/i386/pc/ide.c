/*
 * i686 attachment for the machine-independent PCI IDE driver.
 */

#include "boot.h"
#include "ide.h"
#include "interrupt.h"
#include "pci.h"

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <pci/pciide.h>

#define I386_IDE_WAIT_TICKS     (5u * I386_PIT_HZ)

static struct pciide_softc i386_pciide;
static unsigned char i386_ide_probe_data[DISK_SECTOR_SIZE];

static int
i386_ide_token_equal(const char *text, const char *word)
{
    while (*word != '\0' && *text == *word) {
        ++text;
        ++word;
    }
    return *word == '\0' && (*text == '\0' || *text == ' ');
}

static const char *
i386_ide_token_value(const char *text, const char *name)
{
    while (*name != '\0') {
        if (*text == '\0' || *text != *name)
            return 0;
        ++text;
        ++name;
    }
    return text;
}

static enum pciide_mode_policy
i386_ide_mode_policy(void)
{
    const char *command_line;
    const char *value;

    command_line = i386_boot_command_line();
    while (*command_line != '\0') {
        while (*command_line == ' ')
            ++command_line;
        value = i386_ide_token_value(command_line, "ata=");
        if (value != 0) {
            if (i386_ide_token_equal(value, "pio"))
                return PCIIDE_MODE_PIO;
            if (i386_ide_token_equal(value, "dma"))
                return PCIIDE_MODE_DMA;
            if (i386_ide_token_equal(value, "auto"))
                return PCIIDE_MODE_AUTO;
        }
        while (*command_line != '\0' && *command_line != ' ')
            ++command_line;
    }
    return PCIIDE_MODE_AUTO;
}

static int
i386_ide_irq_establish(void *cookie, unsigned irq,
    pci_interrupt_handler_t handler, void *arg)
{
    (void)cookie;
    if (irq != PCIIDE_PRIMARY_IRQ ||
        !i386_irq_establish(irq, handler, arg))
        return EINVAL;
    i386_pic_unmask(irq);
    return 0;
}

static int
i386_ide_wait(void *cookie, volatile unsigned *done, unsigned ticks)
{
    i386_u32 start;
    int error;
    int state;

    (void)cookie;
    if (done == 0 || ticks == 0)
        return EINVAL;

    /* Autoconfiguration runs in proc0 before the scheduler is available. */
    if (u.u_procp == &proc[0]) {
        state = i386_intr_disable();
        start = i386_pit_ticks();
        while (!*done &&
            (i386_u32)(i386_pit_ticks() - start) < ticks)
            __asm__ volatile ("sti; hlt; cli" : : : "memory");
        error = *done ? 0 : ETIMEDOUT;
        i386_intr_restore(state);
        return error;
    }

    state = splhigh();
    error = 0;
    while (!*done && error == 0)
        error = tsleep((caddr_t)done, PRIBIO, ticks);
    splx(state);
    if (error == EWOULDBLOCK)
        return ETIMEDOUT;
    return error;
}

static void
i386_ide_wakeup(void *cookie, volatile unsigned *done)
{
    (void)cookie;
    wakeup((caddr_t)done);
}

const struct disk_backend_ops *
i386_ide_backend_ops(void)
{
    return pciide_disk_ops(&i386_pciide);
}

void *
i386_ide_backend_arg(void)
{
    return &i386_pciide;
}

disk_sector_t
i386_ide_sector_count(void)
{
    return pciide_sector_count(&i386_pciide);
}

int
i386_ide_probe(void)
{
    struct pciide_attach_args args;
    const struct disk_backend_ops *ops;
    int error;

    bzero(&args, sizeof(args));
    args.pa_device = i386_pci_ide_device();
    args.pa_isa_device = i386_pci_isa_device();
    if (args.pa_device == 0) {
        i386_early_puts("ide-primary-master: none\n");
        return 0;
    }
    args.pa_policy = i386_ide_mode_policy();
    args.pa_command_port = PCIIDE_PRIMARY_COMMAND_PORT;
    args.pa_control_port = PCIIDE_PRIMARY_CONTROL_PORT;
    args.pa_irq = PCIIDE_PRIMARY_IRQ;
    args.pa_wait_ticks = I386_IDE_WAIT_TICKS;
    args.pa_irq_establish = i386_ide_irq_establish;
    args.pa_wait = i386_ide_wait;
    args.pa_wakeup = i386_ide_wakeup;
    error = pciide_attach(&i386_pciide, &args);
    if (error != 0) {
        i386_early_puts("ide-primary-master: none\n");
        i386_early_puts("ide-attach-error: ");
        i386_early_put_hex32((i386_u32)error);
        i386_early_putc('\n');
        return 0;
    }

    i386_early_puts("ide-primary-master: ata\n");
    i386_early_puts("ide-lba28: ok\n");
    ops = pciide_disk_ops(&i386_pciide);
    error = ops->dbo_read(&i386_pciide, 0, 1, i386_ide_probe_data);
    if (error != 0) {
        i386_early_puts("ide-lba0: failed\n");
        return 0;
    }
    i386_early_puts("ide-backend-read: ok\n");
    i386_early_puts("ide-lba0: ok\n");
    if (ops->dbo_read(&i386_pciide,
        pciide_sector_count(&i386_pciide), 1, i386_ide_probe_data) ==
        EINVAL &&
        ops->dbo_read(&i386_pciide,
        pciide_sector_count(&i386_pciide) - 1, 2,
        i386_ide_probe_data) == EINVAL &&
        ops->dbo_read(&i386_pciide, 0, 1, 0) == EINVAL &&
        ops->dbo_read(&i386_pciide, 0, 0, i386_ide_probe_data) == EINVAL)
        i386_early_puts("ide-bounds: ok\n");
    else
        i386_early_puts("ide-bounds: failed\n");
    return 1;
}
