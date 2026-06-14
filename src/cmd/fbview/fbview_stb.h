#ifndef FBVIEW_STB_H
#define FBVIEW_STB_H

unsigned char *fbview_stbi_load_rgb_from_memory(const unsigned char *buffer,
    int len, int *x, int *y, int *comp);
void fbview_stbi_image_free(void *data);
const char *fbview_stbi_failure_reason(void);

#endif
