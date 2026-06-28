#ifdef ETHER_ENABLED
#ifdef ETHER_NUNITS
#define NETHER ETHER_NUNITS
#else
#define NETHER 1
#endif
#else
#define NETHER 0
#endif
