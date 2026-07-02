#include <stdlib.h>

long
labs(long arg)
{
	return(arg < 0 ? -arg : arg);
}
