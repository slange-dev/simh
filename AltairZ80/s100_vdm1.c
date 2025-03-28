/* s100_vdm1.c: Processor Technology VDM-1

   Copyright (c) 2023, Patrick Linstruth

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   VDM1 - Processor Technology VDM-1
*/

#include "altairz80_defs.h"
#include "sim_video.h"

#define VDM1_MARGIN       (5)
#define VDM1_CHAR_XSIZE   (9)                        /* character x size */
#define VDM1_CHAR_YSIZE   (13)                       /* character y size */
#define VDM1_COLS         (64)                       /* number of colums */
#define VDM1_LINES        (16)                       /* number of rows */
#define VDM1_XSIZE        (VDM1_COLS * VDM1_CHAR_XSIZE + VDM1_MARGIN * 2)   /* visible width */
#define VDM1_YSIZE        (VDM1_LINES * VDM1_CHAR_YSIZE + VDM1_MARGIN * 2)  /* visible height */
#define VDM1_PIXELS       (VDM1_XSIZE * VDM1_YSIZE)  /* total number of pixels */

#define VDM1_MEM_BASE    0xcc00
#define VDM1_MEM_SIZE    1024
#define VDM1_MEM_MASK    (1024 - 1)

#define VDM1_IO_BASE     0xfe
#define VDM1_IO_SIZE     1

/*
** PORT ASSIGNMENTS
*/
#define VDM1_DSTAT_RMSK  0xf0    /* START ROW MASK */
#define VDM1_DSTAT_CMSK  0x0f    /* START COL MASK */

/*
** Public VID_DISPLAY for other devices that may want
** to access the video display directly, such as keyboard
** events.
*/
VID_DISPLAY *vdm1_vptr = NULL;
t_stat (*vdm1_kb_callback)(SIM_KEY_EVENT *kev) = NULL;

static uint8 vdm1_ram[VDM1_MEM_SIZE];
static uint8 vdm1_dstat = 0x00;
static t_bool vdm1_dirty = TRUE;
static t_bool vdm1_reverse = FALSE;
static t_bool vdm1_blink = FALSE;
static uint16 vdm1_counter = 0;
static t_bool vdm1_active = FALSE;
static uint32 vdm1_surface[VDM1_PIXELS];
static uint32 vdm1_palette[2];

enum vdm1_switch {VDM1_NONE,
    VDM1_NORMAL, VDM1_REVERSE, VDM1_BLINK, VDM1_NOBLINK,
    VDM1_MODE1, VDM1_MODE2, VDM1_MODE3, VDM1_MODE4
};

static enum vdm1_switch vdm1_ctrl = VDM1_MODE4;
static enum vdm1_switch vdm1_cursor = VDM1_NOBLINK;
static enum vdm1_switch vdm1_display = VDM1_NORMAL;

static const uint8 vdm1_charset[128][VDM1_CHAR_YSIZE] =
 {{0x00,0x7f,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x7f,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x7f,0x00,0x00,0x00},
  {0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x7f,0x00,0x00,0x00},
  {0x00,0x20,0x10,0x08,0x04,0x3e,0x10,0x08,0x04,0x02,0x00,0x00,0x00},
  {0x00,0x7f,0x41,0x63,0x55,0x49,0x55,0x63,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x00,0x01,0x02,0x04,0x48,0x50,0x60,0x40,0x00,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x41,0x7f,0x14,0x14,0x77,0x00,0x00,0x00},
  {0x00,0x10,0x20,0x7c,0x22,0x11,0x01,0x01,0x01,0x01,0x00,0x00,0x00},
  {0x00,0x00,0x08,0x04,0x02,0x7f,0x02,0x04,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x7f,0x00,0x00,0x00,0x7f,0x00,0x00,0x00,0x7f,0x00,0x00,0x00},
  {0x00,0x00,0x08,0x08,0x08,0x49,0x2a,0x1c,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x2a,0x1c,0x08,0x49,0x2a,0x1c,0x08,0x00,0x00,0x00},
  {0x00,0x00,0x08,0x10,0x20,0x7f,0x20,0x10,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x63,0x55,0x49,0x55,0x63,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x49,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x7f,0x41,0x41,0x41,0x7f,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x1c,0x2a,0x49,0x49,0x4f,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x4f,0x49,0x49,0x2a,0x1c,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x79,0x49,0x49,0x2a,0x1c,0x00,0x00,0x00},
  {0x00,0x1c,0x2a,0x49,0x49,0x79,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x00,0x11,0x0a,0x04,0x4a,0x51,0x60,0x40,0x00,0x00,0x00,0x00},
  {0x00,0x3e,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x63,0x00,0x00,0x00},
  {0x00,0x01,0x01,0x01,0x01,0x7f,0x01,0x01,0x01,0x01,0x00,0x00,0x00},
  {0x00,0x7f,0x41,0x22,0x14,0x08,0x14,0x22,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x1c,0x1c,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
  {0x00,0x3c,0x42,0x42,0x40,0x30,0x08,0x08,0x00,0x08,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x7f,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x7f,0x49,0x49,0x49,0x79,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x7f,0x41,0x41,0x41,0x79,0x49,0x49,0x49,0x7f,0x00,0x00,0x00},
  {0x00,0x7f,0x41,0x41,0x41,0x4f,0x49,0x49,0x49,0x7f,0x00,0x00,0x00},
  {0x00,0x7f,0x49,0x49,0x49,0x4f,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x08,0x08,0x00,0x00,0x00},
  {0x00,0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x14,0x14,0x14,0x7f,0x14,0x7f,0x14,0x14,0x14,0x00,0x00,0x00},
  {0x00,0x08,0x3f,0x48,0x48,0x3e,0x09,0x09,0x7e,0x08,0x00,0x00,0x00},
  {0x00,0x20,0x51,0x22,0x04,0x08,0x10,0x22,0x45,0x02,0x00,0x00,0x00},
  {0x00,0x38,0x44,0x44,0x28,0x10,0x29,0x46,0x46,0x39,0x00,0x00,0x00},
  {0x00,0x0c,0x0c,0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x04,0x08,0x10,0x10,0x10,0x10,0x10,0x08,0x04,0x00,0x00,0x00},
  {0x00,0x10,0x08,0x04,0x04,0x04,0x04,0x04,0x08,0x10,0x00,0x00,0x00},
  {0x00,0x00,0x08,0x49,0x2a,0x1c,0x2a,0x49,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x08,0x08,0x08,0x7f,0x08,0x08,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10,0x20,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x7f,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
  {0x00,0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00,0x00,0x00},
  {0x00,0x3e,0x41,0x43,0x45,0x49,0x51,0x61,0x41,0x3e,0x00,0x00,0x00},
  {0x00,0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x08,0x3e,0x00,0x00,0x00},
  {0x00,0x3e,0x41,0x01,0x02,0x1c,0x20,0x40,0x40,0x7f,0x00,0x00,0x00},
  {0x00,0x3e,0x41,0x01,0x01,0x1e,0x01,0x01,0x41,0x3e,0x00,0x00,0x00},
  {0x00,0x02,0x06,0x0a,0x12,0x22,0x42,0x7f,0x02,0x02,0x00,0x00,0x00},
  {0x00,0x7f,0x40,0x40,0x7c,0x02,0x01,0x01,0x42,0x3c,0x00,0x00,0x00},
  {0x00,0x1e,0x20,0x40,0x40,0x7e,0x41,0x41,0x41,0x3e,0x00,0x00,0x00},
  {0x00,0x7f,0x41,0x02,0x04,0x08,0x10,0x10,0x10,0x10,0x00,0x00,0x00},
  {0x00,0x3e,0x41,0x41,0x41,0x3e,0x41,0x41,0x41,0x3e,0x00,0x00,0x00},
  {0x00,0x3e,0x41,0x41,0x41,0x3f,0x01,0x01,0x02,0x3c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x10,0x20,0x00},
  {0x00,0x04,0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x04,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3e,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x10,0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x10,0x00,0x00,0x00},
  {0x00,0x1e,0x21,0x21,0x01,0x06,0x08,0x08,0x00,0x08,0x00,0x00,0x00},
  {0x00,0x1e,0x21,0x4d,0x55,0x55,0x5e,0x40,0x20,0x1e,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x41,0x7f,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x00,0x7e,0x21,0x21,0x21,0x3e,0x21,0x21,0x21,0x7e,0x00,0x00,0x00},
  {0x00,0x1e,0x21,0x40,0x40,0x40,0x40,0x40,0x21,0x1e,0x00,0x00,0x00},
  {0x00,0x7c,0x22,0x21,0x21,0x21,0x21,0x21,0x22,0x7c,0x00,0x00,0x00},
  {0x00,0x7f,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x7f,0x00,0x00,0x00},
  {0x00,0x7f,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x00,0x1e,0x21,0x40,0x40,0x40,0x4f,0x41,0x21,0x1e,0x00,0x00,0x00},
  {0x00,0x41,0x41,0x41,0x41,0x7f,0x41,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x00,0x3e,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x3e,0x00,0x00,0x00},
  {0x00,0x1f,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x38,0x00,0x00,0x00},
  {0x00,0x41,0x42,0x44,0x48,0x50,0x68,0x44,0x42,0x41,0x00,0x00,0x00},
  {0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7f,0x00,0x00,0x00},
  {0x00,0x41,0x63,0x55,0x49,0x49,0x41,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x00,0x41,0x61,0x51,0x49,0x45,0x43,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x41,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x7e,0x41,0x41,0x41,0x7e,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x00,0x1c,0x22,0x41,0x41,0x41,0x49,0x45,0x22,0x1d,0x00,0x00,0x00},
  {0x00,0x7e,0x41,0x41,0x41,0x7e,0x48,0x44,0x42,0x41,0x00,0x00,0x00},
  {0x00,0x3e,0x41,0x40,0x40,0x3e,0x01,0x01,0x41,0x3e,0x00,0x00,0x00},
  {0x00,0x7f,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
  {0x00,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x3e,0x00,0x00,0x00},
  {0x00,0x41,0x41,0x41,0x22,0x22,0x14,0x14,0x08,0x08,0x00,0x00,0x00},
  {0x00,0x41,0x41,0x41,0x41,0x49,0x49,0x55,0x63,0x41,0x00,0x00,0x00},
  {0x00,0x41,0x41,0x22,0x14,0x08,0x14,0x22,0x41,0x41,0x00,0x00,0x00},
  {0x00,0x41,0x41,0x22,0x14,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
  {0x00,0x7f,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x7f,0x00,0x00,0x00},
  {0x00,0x3c,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x3c,0x00,0x00,0x00},
  {0x00,0x00,0x40,0x20,0x10,0x08,0x04,0x02,0x01,0x00,0x00,0x00,0x00},
  {0x00,0x3c,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x3c,0x00,0x00,0x00},
  {0x00,0x08,0x14,0x22,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7f,0x00,0x00,0x00},
  {0x00,0x18,0x18,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3c,0x02,0x3e,0x42,0x42,0x3d,0x00,0x00,0x00},
  {0x00,0x40,0x40,0x40,0x5c,0x62,0x42,0x42,0x62,0x5c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3c,0x42,0x40,0x40,0x42,0x3c,0x00,0x00,0x00},
  {0x00,0x02,0x02,0x02,0x3a,0x46,0x42,0x42,0x46,0x3a,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3c,0x42,0x7e,0x40,0x40,0x3c,0x00,0x00,0x00},
  {0x00,0x0c,0x12,0x10,0x10,0x7c,0x10,0x10,0x10,0x10,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3a,0x46,0x42,0x46,0x3a,0x02,0x02,0x42,0x3c},
  {0x00,0x40,0x40,0x40,0x5c,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00},
  {0x00,0x00,0x08,0x00,0x18,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x06,0x02,0x02,0x02,0x02,0x02,0x02,0x22,0x1c},
  {0x00,0x40,0x40,0x40,0x44,0x48,0x50,0x68,0x44,0x42,0x00,0x00,0x00},
  {0x00,0x18,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x5c,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3c,0x42,0x42,0x42,0x42,0x3c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x5c,0x62,0x42,0x42,0x62,0x5c,0x40,0x40,0x40},
  {0x00,0x00,0x00,0x00,0x3a,0x46,0x42,0x42,0x46,0x3a,0x02,0x02,0x02},
  {0x00,0x00,0x00,0x00,0x5c,0x62,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x3c,0x42,0x30,0x0c,0x42,0x3c,0x00,0x00,0x00},
  {0x00,0x00,0x10,0x10,0x7c,0x10,0x10,0x10,0x12,0x0c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3a,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x41,0x41,0x41,0x22,0x14,0x08,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x41,0x49,0x49,0x49,0x49,0x36,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3a,0x02,0x42,0x3c},
  {0x00,0x00,0x00,0x00,0x7e,0x04,0x08,0x10,0x20,0x7e,0x00,0x00,0x00},
  {0x00,0x0e,0x10,0x10,0x10,0x20,0x10,0x10,0x10,0x0e,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x00,0x00,0x08,0x08,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x18,0x04,0x04,0x04,0x02,0x04,0x04,0x04,0x18,0x00,0x00,0x00},
  {0x00,0x30,0x49,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x24,0x49,0x12,0x24,0x49,0x12,0x24,0x49,0x12,0x00,0x00,0x00}};

/* Debugging Bitmaps */

#define DBG_REG         0x0001                          /* registers */

extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat exdep_cmd(int32 flag, CONST char *cptr);

static t_stat vdm1_svc(UNIT *uptr);
static t_stat vdm1_reset(DEVICE *dptr);
static t_stat vdm1_boot(int32 unitno, DEVICE *dptr);
static t_stat vdm1_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static int32 vdm1_io(const int32 port, const int32 io, const int32 data);
static int32 vdm1_mem(int32 addr, int32 rw, int32 data);
static const char *vdm1_description(DEVICE *dptr);
static void vdm1_refresh(void);
static void vdm1_render(void);
static void vdm1_render_char(uint8 byte, uint8 x, uint8 y);
static t_stat vdm1_set_ctrl(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vdm1_show_ctrl(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vdm1_set_cursor(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vdm1_show_cursor(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vdm1_set_display(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vdm1_show_display(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* VDM1 data structures

   vdm1_dev      VDM1 device descriptor
   vdm1_unit     VDM1 unit descriptor
   vdm1_reg      VDM1 register list
*/

typedef struct {
    PNP_INFO pnp;        /* Must be first    */
} VDM1_CTX;

static VDM1_CTX vdm1_ctx = {{VDM1_MEM_BASE, VDM1_MEM_SIZE, VDM1_IO_BASE, VDM1_IO_SIZE}};

UNIT vdm1_unit = {
    UDATA (&vdm1_svc, 0, 0), 25000
};

REG vdm1_reg[] = {
    { HRDATAD (DSTAT, vdm1_dstat, 8, "VDM-1 display parameter register"), },
    { HRDATAD (DIRTY, vdm1_dirty, 1, "VDM-1 dirty register"), },
    { HRDATAD (BLINK, vdm1_blink, 1, "VDM-1 blink register"), },
    { NULL }
};

DEBTAB vdm1_debug[] = {
    { "REG",     DBG_REG,     "Register activity" },
    { "VIDEO",   SIM_VID_DBG_VIDEO,     "Video activity" },
    { 0 }
};


MTAB vdm1_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "VDM-1 base I/O address"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "MEMBASE",  "MEMBASE",
        &set_membase, &show_membase, NULL, "VDM-1 base memory address"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "CTRL",  "CTRL",
        &vdm1_set_ctrl, &vdm1_show_ctrl, NULL, "VDM-1 control character switches"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "CURSOR",  "CURSOR",
        &vdm1_set_cursor, &vdm1_show_cursor, NULL, "VDM-1 cursor switches"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "DISPLAY",  "DISPLAY",
        &vdm1_set_display, &vdm1_show_display, NULL, "VDM-1 display switches"   },
    { 0 }
};
DEVICE vdm1_dev = {
    "VDM1", &vdm1_unit, vdm1_reg, vdm1_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &vdm1_reset,
    &vdm1_boot, NULL, NULL,
    &vdm1_ctx, DEV_DEBUG | DEV_DIS | DEV_DISABLE, 0,
    vdm1_debug, NULL, NULL, &vdm1_help, NULL, NULL,
    &vdm1_description
};

/* VDM1 routines

   vdm1_svc      process event
   vdm1_reset    process reset
*/

t_stat vdm1_svc(UNIT *uptr)
{
    SIM_KEY_EVENT kev;

    vdm1_counter++;

    /* Handle blink */
    if ((vdm1_counter % 10 == 0) && (vdm1_cursor == VDM1_BLINK)) {
        vdm1_blink = !vdm1_blink;
        vdm1_dirty = TRUE;
    }

    if (vdm1_dirty) {
        vdm1_refresh();
        vdm1_dirty = TRUE;
    }

    if (vdm1_kb_callback != NULL) {
        if (vid_poll_kb(&kev) == SCPE_OK) {
            (*vdm1_kb_callback)(&kev);
        }
    }

    sim_activate_after_abs(uptr, uptr->wait); // 25ms refresh rate

    return SCPE_OK;
}

t_stat vdm1_reset(DEVICE *dptr)
{
    VDM1_CTX *xptr;
    t_stat r;
    int i;

    xptr = (VDM1_CTX *) dptr->ctxt;

    if (dptr->flags & DEV_DIS) {
        sim_map_resource(xptr->pnp.mem_base, xptr->pnp.mem_size, RESOURCE_TYPE_MEMORY, &vdm1_mem, "vdm1", TRUE);
        sim_map_resource(xptr->pnp.io_base, xptr->pnp.io_size, RESOURCE_TYPE_IO, &vdm1_io, "vdm1", TRUE);

        sim_cancel(&vdm1_unit);

        if (vdm1_active) {
            vdm1_active = FALSE;
            return vid_close();
        }

        return SCPE_OK;
    }

    sim_map_resource(xptr->pnp.mem_base, xptr->pnp.mem_size, RESOURCE_TYPE_MEMORY, &vdm1_mem, "vdm1", FALSE);
    sim_map_resource(xptr->pnp.io_base, xptr->pnp.io_size, RESOURCE_TYPE_IO, &vdm1_io, "vdm1", FALSE);

    if (!vdm1_active)  {
        r = vid_open_window(&vdm1_vptr, &vdm1_dev, "Display", VDM1_XSIZE, VDM1_YSIZE, SIM_VID_IGNORE_VBAR | SIM_VID_RESIZABLE); /* video buffer size */

        if (r != SCPE_OK) {
            return r;
        }

        vid_set_window_size(vdm1_vptr, VDM1_XSIZE, VDM1_YSIZE * 2);

        vdm1_palette[0] = vid_map_rgb_window(vdm1_vptr, 0x00, 0x00, 0x00);
        vdm1_palette[1] = vid_map_rgb_window(vdm1_vptr, 0x00, 0xFF, 0x30);

        for (i = 0; i < VDM1_PIXELS; i++) {
            vdm1_surface[i] = vdm1_palette[0];
        }

        vdm1_active = TRUE;
    }

    sim_activate_after_abs(&vdm1_unit, 25);

    return SCPE_OK;
}

static t_stat vdm1_boot(int32 unitno, DEVICE *dptr)
{
    exdep_cmd(EX_D, "-m 00 MVI A,0");
    exdep_cmd(EX_D, "-m 02 OUT 0FEH");
    exdep_cmd(EX_D, "-m 04 MVI C,0");
    exdep_cmd(EX_D, "-m 06 MVI B,0");
    exdep_cmd(EX_D, "-m 08 LXI H,0CC00H");
    exdep_cmd(EX_D, "-m 0B DCR B");
    exdep_cmd(EX_D, "-m 0C MOV M,B");
    exdep_cmd(EX_D, "-m 0D INX H");
    exdep_cmd(EX_D, "-m 0E MOV A,H");
    exdep_cmd(EX_D, "-m 0F CPI 0D0H");
    exdep_cmd(EX_D, "-m 11 JNZ 000BH");
    exdep_cmd(EX_D, "-m 14 DCX H");
    exdep_cmd(EX_D, "-m 15 MOV A,H");
    exdep_cmd(EX_D, "-m 16 ORA A");
    exdep_cmd(EX_D, "-m 17 JNZ 0014H");
    exdep_cmd(EX_D, "-m 1A INR C");
    exdep_cmd(EX_D, "-m 1B MOV B,C");
    exdep_cmd(EX_D, "-m 1C JMP 0008H");

    *((int32 *) sim_PC->loc) = 0x0000;

    return SCPE_OK;
}

static int32 vdm1_io(const int32 port, const int32 io, const int32 data) {
    if (io == 1) {
        vdm1_dstat = data & 0xff;
    }
    return 0xff;
}

/*
 * VDM-1 1K Video Memory (16 x 64 characters)
 */
static int32 vdm1_mem(int32 addr, int32 rw, int32 data)
{

    if (rw == 0) {
        data = vdm1_ram[addr & VDM1_MEM_MASK];
    }
    else {
        vdm1_ram[addr & VDM1_MEM_MASK] = data;
        vdm1_dirty = TRUE;
    }

    return data;
}

t_stat vdm1_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "\nThe VDM-1 has several switches that control the video display:\n\n");

    fprintf(st, "SET VDM1 CTRL=MODEx\n");
    fprintf(st, "MODE1   - All control characters suppressed. Only cursor blocks\n");
    fprintf(st, "          are displayed. CR and VT enabled.\n");
    fprintf(st, "MODE2   - Control characters blanked. CR and VT enabled.\n");
    fprintf(st, "MODE3   - Control characters displayable. CR and VT enabled.\n");
    fprintf(st, "MODE4   - Control characters displayable. CR and VT disabled. (default)\n\n");

    fprintf(st, "SET VDM1 CURSOR=NONE,BLINK,NOBLINK\n");
    fprintf(st, "NONE    - All cursors suppressed.\n");
    fprintf(st, "BLINK   - Blinking cursor.\n");
    fprintf(st, "NOBLINK - Non-blinking cursor. (default)\n\n");

    fprintf(st, "SET VDM1 DISPLAY=NONE,NORMAL,REVERSE\n");
    fprintf(st, "NONE    - No display.\n");
    fprintf(st, "NORMAL  - Normal video. (default)\n");
    fprintf(st, "REVERSE - Reverse video.\n\n");

    fprintf(st, "VDM-1 test program displays all characters on the screen:\n");
    fprintf(st, "BOOT VDM1\n\n");

    return SCPE_OK;
}

const char *vdm1_description (DEVICE *dptr)
{
    return "Processor Technology VDM-1 Display";
}

/*
 * Draw and refresh the screen in the video window
 */
static void vdm1_refresh(void) {
    if (vdm1_active) {
        vdm1_render();
        vid_draw_window(vdm1_vptr, VDM1_MARGIN, VDM1_MARGIN, VDM1_XSIZE, VDM1_YSIZE, vdm1_surface);
        vid_refresh_window(vdm1_vptr);
    }
}

/*
 * The VDM-1 display is make up of 16 64-character rows. Each character occupies
 * 1 byte in memory from CC00-CFFF.
 */
static void vdm1_render(void)
{
    uint8 x,y,s,c,c1;
    int addr = 0;
    t_bool eol_blank = FALSE;
    t_bool eos_blank = FALSE;

    addr += (vdm1_dstat & VDM1_DSTAT_CMSK) * VDM1_COLS;
    s = (vdm1_dstat & VDM1_DSTAT_RMSK) >> 4; /* Shadowing */

    for (y = 0; y < VDM1_LINES; y++) {
        for (x = 0; x < VDM1_COLS; x++) {

            c = vdm1_ram[addr++];
            c1 = c & 0x7f;

            /* EOL and EOS blanking */
            if (c1 == 0x0d && (vdm1_ctrl == VDM1_MODE2 || vdm1_ctrl == VDM1_MODE3)) {    // CR
                eol_blank = TRUE;
            }
            else if (c1 == 0x0b && (vdm1_ctrl == VDM1_MODE2 || vdm1_ctrl == VDM1_MODE3)) {    // VT
                eos_blank = TRUE;
            }

            /* Blanking control */
            if (vdm1_display == VDM1_NONE || eol_blank || eos_blank || y < s) {
                c = ' ';
            }

            /* Control character suppression */
            if ((c1 < 0x20 || c1  == 0x7f) && (vdm1_ctrl == VDM1_MODE1 || vdm1_ctrl == VDM1_MODE2)) {
                c = ' ';
            }

            vdm1_render_char(c, x, y);

            if (addr == VDM1_MEM_SIZE) {
                addr = 0;
            }
        }
    }
}

/*
 * The VDM-1 rendered characters one scan line at a time.
 * The simulator renders an entire character at a time by
 * rendering each character in a rectangle area in the
 * video surface buffer.
 */
static void vdm1_render_char(uint8 byte, uint8 x, uint8 y)
{
    uint8 rx,ry,c;
    int start,pixel;

    start = (x * VDM1_CHAR_XSIZE) + (VDM1_XSIZE * VDM1_CHAR_YSIZE * y);

    for (ry = 0; ry < VDM1_CHAR_YSIZE; ry++) {

        pixel = start + (VDM1_XSIZE * ry);

        c = vdm1_charset[byte & 0x7f][ry];

        if (!vdm1_blink && byte & 0x80) {
            c = ~(c & 0xff);
        }

        if (vdm1_display == VDM1_REVERSE) {
            c = ~(c & 0xff);
        }

        for (rx = 0; rx < VDM1_CHAR_XSIZE - 1; rx++) {
            vdm1_surface[pixel++] = vdm1_palette[c & (0x80 >> rx) ? !vdm1_reverse : vdm1_reverse];
        }

        vdm1_surface[pixel++] = vdm1_palette[(c & 0x80) ? !vdm1_reverse : vdm1_reverse];
    }
}

static t_stat vdm1_set_ctrl(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "MODE1", strlen(cptr))) {
        vdm1_ctrl = VDM1_MODE1;
    } else if (!strncmp(cptr, "MODE2", strlen(cptr))) {
        vdm1_ctrl = VDM1_MODE2;
    } else if (!strncmp(cptr, "MODE3", strlen(cptr))) {
        vdm1_ctrl = VDM1_MODE3;
    } else if (!strncmp(cptr, "MODE4", strlen(cptr))) {
        vdm1_ctrl = VDM1_MODE4;
    } else {
        return SCPE_ARG;
    }

    vdm1_dirty = TRUE;

    return SCPE_OK;
}

static t_stat vdm1_show_ctrl(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (!st) return SCPE_IERR;

    fprintf(st, "CTRL=");

    switch (vdm1_ctrl) {
        case VDM1_MODE1:
            fprintf(st, "MODE1");
            break;

        case VDM1_MODE2:
            fprintf(st, "MODE2");
            break;

        case VDM1_MODE3:
            fprintf(st, "MODE3");
            break;

        case VDM1_MODE4:
            fprintf(st, "MODE4");
            break;

        default:
            fprintf(st, "UNKNOWN");
            break;
    }

    return SCPE_OK;
}

static t_stat vdm1_set_cursor(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "NONE", strlen(cptr))) {
        vdm1_cursor = VDM1_NONE;
    } else if (!strncmp(cptr, "BLINK", strlen(cptr))) {
        vdm1_cursor = VDM1_BLINK;
    } else if (!strncmp(cptr, "NOBLINK", strlen(cptr))) {
        vdm1_cursor = VDM1_NOBLINK;
        vdm1_blink = FALSE;
    } else {
        return SCPE_ARG;
    }

    vdm1_dirty = TRUE;

    return SCPE_OK;
}

static t_stat vdm1_show_cursor(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (!st) return SCPE_IERR;

    fprintf(st, "CURSOR=");

    switch (vdm1_cursor) {
        case VDM1_NONE:
            fprintf(st, "NONE");
            break;

        case VDM1_BLINK:
            fprintf(st, "BLINK");
            break;

        case VDM1_NOBLINK:
            fprintf(st, "NOBLINK");
            break;

        default:
            fprintf(st, "UNKNOWN");
            break;
    }

    return SCPE_OK;
}

static t_stat vdm1_set_display(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "NONE", strlen(cptr))) {
        vdm1_display = VDM1_NONE;
    } else if (!strncmp(cptr, "NORMAL", strlen(cptr))) {
        vdm1_display = VDM1_NORMAL;
    } else if (!strncmp(cptr, "REVERSE", strlen(cptr))) {
        vdm1_display = VDM1_REVERSE;
    } else {
        return SCPE_ARG;
    }

    vdm1_dirty = TRUE;

    return SCPE_OK;
}

static t_stat vdm1_show_display(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (!st) return SCPE_IERR;

    fprintf(st, "DISPLAY=");

    switch (vdm1_display) {
        case VDM1_NONE:
            fprintf(st, "NONE");
            break;

        case VDM1_NORMAL:
            fprintf(st, "NORMAL");
            break;

        case VDM1_REVERSE:
            fprintf(st, "REVERSE");
            break;

        default:
            fprintf(st, "UNKNOWN");
            break;
    }

    return SCPE_OK;
}

