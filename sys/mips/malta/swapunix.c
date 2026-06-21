#include "sys/param.h"
#include "sys/conf.h"

dev_t	rootdev = makedev(0, 0);	/* romdisk0 */
dev_t	dumpdev = makedev(1, 0);	/* ramswap0 */
dev_t	swapdev = makedev(1, 0);	/* ramswap0 */
