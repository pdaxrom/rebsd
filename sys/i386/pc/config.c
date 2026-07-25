#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/kernel.h>
#include <sys/proc.h>

int nproc = NPROC;
struct proc proc[NPROC];
