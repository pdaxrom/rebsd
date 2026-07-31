/* Host USB tests do not link the kernel sysctl implementation. */

#include <sys/hw_inventory_provider.h>

void
hw_inventory_register_usb(hw_usb_inventory_provider_t provider)
{
    (void)provider;
}
