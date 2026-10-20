#ifndef __GIF_H__
#define __GIF_H__

#include "typedef.h"
#include "asm/imb.h"
#include "res/resfile.h"

struct gif_file_info {
    u16 width;
    u16 height;
    u16 frame_num;
    u16 delay;
    u16 version;
};

struct gif_frame_info {
    u32 addr;
    u32 size;
    int type;
    int subtype;
};

int gif_read_head_info(int prj, RESFILE *specfile, struct image_file *f, struct gif_file_info *info);
int gif_read_frame_info(int prj, RESFILE *specfile, struct image_file *f, struct gif_frame_info *info, struct gif_file_info *gif_info, int index);
int gif_frame_decode(int root_index, struct imb_task *task, int elm_id);
int gif_frame_decode_tobuf(int prj, RESFILE *fp, u32 start_addr, struct image_file *f, int index, u8 **image_index, u8 **image_lut, struct gif_file_info *gif_info, int use_filesystem);

#endif


