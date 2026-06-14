#include <stdlib.h>

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "fbview_stb.h"

unsigned char *
fbview_stbi_load_rgb_from_memory(const unsigned char *buffer, int len,
    int *x, int *y, int *comp)
{
    return stbi_load_from_memory(buffer, len, x, y, comp, 3);
}

void
fbview_stbi_image_free(void *data)
{
    stbi_image_free(data);
}

const char *
fbview_stbi_failure_reason(void)
{
    const char *reason = stbi_failure_reason();

    return reason ? reason : "decode failed";
}
