/*
 * TJpgDec configuration for the ReBSD framebuffer viewer.
 */

#ifndef FBVIEW_TJPGDCNF_H
#define FBVIEW_TJPGDCNF_H

/* Read the image in filesystem-friendly sectors. */
#define JD_SZBUF       512

/* Output MCU blocks as RGB888 for conversion to the active framebuffer. */
#define JD_FORMAT      0

/* Enable 1/2, 1/4 and 1/8 DCT downscaling. */
#define JD_USE_SCALE   1

/* Favor the 32-bit MIPS cores used by Ci20 and N64. */
#define JD_TBLCLIP     1
#define JD_FASTDECODE  1

#endif
