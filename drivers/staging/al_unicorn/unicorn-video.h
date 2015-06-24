#ifndef UNICORN_VIDEO_H
#define UNICORN_VIDEO_H

#define VID_CHANNEL_NUM 2

#define DMA_CONTROL_START_POS                0
#define DMA_CONTROL_START_MASK               (1<<DMA_CONTROL_START_POS)
#define DMA_CONTROL_START                    DMA_CONTROL_START_MASK

#define DMA_CONTROL_RESET_POS                1
#define DMA_CONTROL_RESET_MASK               (1<<DMA_CONTROL_RESET_POS)
#define DMA_CONTROL_RESET                    DMA_CONTROL_RESET_MASK

#define DMA_CONTROL_AUTO_START_POS           2
#define DMA_CONTROL_AUTO_START_MASK          (1<<DMA_CONTROL_AUTO_START_POS)
#define DMA_CONTROL_AUTO_START               DMA_CONTROL_AUTO_START_MASK

#define VIDEO_CONTROL_ENABLE_POS             0
#define VIDEO_CONTROL_ENABLE_MASK            (1<<VIDEO_CONTROL_ENABLE_POS)
#define VIDEO_CONTROL_ENABLE                 VIDEO_CONTROL_ENABLE_MASK

#define VIDEO_CONTROL_INPUT_SEL_POS          1
#define VIDEO_CONTROL_INPUT_SEL_MASK         (7<<VIDEO_CONTROL_INPUT_SEL_POS)


#define VIDEO_CONTROL_TIMESTAMP_INSERT_POS   4
#define VIDEO_CONTROL_TIMESTAMP_INSERT_MASK  (1<<VIDEO_CONTROL_TIMESTAMP_INSERT_POS)
#define VIDEO_CONTROL_TIMESTAMP_INSERT       VIDEO_CONTROL_TIMESTAMP_INSERT_MASK

#define VIDEO_CONTROL_IN_0_RESET_POS
#define VIDEO_CONTROL_IN_0_RESET_MASK        (1<<VIDEO_CONTROL_IN_0_RESET_POS)
#define VIDEO_CONTROL_IN_0_RESET             VIDEO_CONTROL_IN_0_RESET_MASK

#define VIDEO_CONTROL_IN_1_RESET_POS
#define VIDEO_CONTROL_IN_1_RESET_MASK        (1<<VIDEO_CONTROL_IN_1_RESET_POS)
#define VIDEO_CONTROL_IN_1_RESET             VIDEO_CONTROL_IN_1_RESET_MASK

#define VIDEO_CONTROL_IN_2_RESET_POS
#define VIDEO_CONTROL_IN_2_RESET_MASK        (1<<VIDEO_CONTROL_IN_2_RESET_POS)
#define VIDEO_CONTROL_IN_2_RESET             VIDEO_CONTROL_IN_2_RESET_MASK

#define VIDEO_CONTROL_IN_3_RESET_POS
#define VIDEO_CONTROL_IN_3_RESET_MASK        (1<<VIDEO_CONTROL_IN_3_RESET_POS)
#define VIDEO_CONTROL_IN_3_RESET             VIDEO_CONTROL_IN_3_RESET_MASK

#define VIDEO_CONTROL_IN_MIRE_RESET_POS
#define VIDEO_CONTROL_IN_MIRE_RESET_MASK     (1<<VIDEO_CONTROL_IN_MIRE_RESET_POS)
#define VIDEO_CONTROL_IN_MIRE_RESET          VIDEO_CONTROL_IN_MIRE_RESET_MASK

#include "unicorn.h"

extern struct sram_channel *channel0;
extern struct sram_channel *channel1;

extern struct video_device unicorn_video_template0;
extern struct video_device unicorn_video_template1;

extern struct video_device unicorn_videoioctl_template;

int video_mmap(struct file *file, struct vm_area_struct *vma);
int unicorn_video_register(struct unicorn_dev *dev, int chan_num,
         struct video_device *video_template);
void unicorn_video_unregister(struct unicorn_dev *dev, int chan_num);
struct unicorn_fmt *format_by_fourcc(unsigned int fourcc);
int unicorn_start_video_dma(struct unicorn_dev *dev,
          struct unicorn_buffer *buf,
          struct unicorn_fh *fh);
int unicorn_recover_fifo_full_error(struct unicorn_dev *dev,
                                    int channel);
int unicorn_continue_video_dma(struct unicorn_dev *dev,
                               struct unicorn_buffer *buf,
                               struct unicorn_fh *fh,
                               int buff_index);
int unicorn_video_dma_flipflop_buf(struct unicorn_dev *dev,
          struct unicorn_buffer *buf,
          struct unicorn_fh *fh);
int unicorn_init_video(struct unicorn_dev *dev);
int unicorn_video_change_fps(struct unicorn_dev *dev, struct unicorn_fh *fh);

struct camera_to_probe_t
{
  char *name;
  int i2c_addr;
  struct i2c_adapter *i2c_adapter;
};

#endif
