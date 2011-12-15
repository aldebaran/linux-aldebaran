/* <LIC_AMD_STD>
 * Copyright (c) 2004-2005 Advanced Micro Devices, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING
 * </LIC_AMD_STD>  */
/* <CTL_AMD_STD>
 * </CTL_AMD_STD>  */
/* <DOC_AMD_STD>
  * Base include file for the Cimarron library. This file should be modified 
  * and included in any Cimarron-based project.
 * </DOC_AMD_STD>  */

/*----------------------------------------------------------------------*/
/* MODULE SETTINGS                                                      */
/* The following #defines affect which modules are included in the      */
/* project.                                                             */
/*----------------------------------------------------------------------*/

#define CIMARRON_INCLUDE_GP                1
#define CIMARRON_INCLUDE_VG                1
#define CIMARRON_INCLUDE_VIP               1
#define CIMARRON_INCLUDE_VOP               1
#define CIMARRON_INCLUDE_VIDEO             1
#define CIMARRON_INCLUDE_INIT              1

#define CIMARRON_INCLUDE_VG_READ_ROUTINES  1
#define CIMARRON_INCLUDE_DF_READ_ROUTINES  1
#define CIMARRON_INCLUDE_VIP_READ_ROUTINES 1
#define CIMARRON_INCLUDE_VOP_READ_ROUTINES 1

/*----------------------------------------------------------------------*/
/* HARDWARE ACCESS SETTINGS                                             */
/* The following #defines affect how the Cimarron macros access the     */
/* hardware.  The hardware access macros are broken up into groups.     */
/* Each group includes an enabling #define as well as several #define   */
/* options that modify the macro configuration that is included.        */
/* If the enabling define is deleted or all options are set to 0, the   */
/* corresponding macros must be implemented by the user.   The          */
/* combinations are explained as follows:                               */
/* must be manually defined by the user. This allows a user to use the  */
/* cim_defs.h file for only those macros that suit the needs of his/her */
/* project.  For example, a user may need custom implementations of the */
/* I/O and MSR macros, but may still want to use the default macros to  */
/* read and write hardware registers. The combinations are explained as */
/* follows:                                                             */
/*                                                                      */
/* Register Group:                                                      */
/*   Disabling define:                                                  */
/*       CIMARRON_EXCLUDE_REGISTER_ACCESS_MACROS                        */
/*          Define this setting to exclude the register access macros.  */
/*          This setting is the inverse of the other group settings in  */
/*          that these macros are included by default.  This allows the */
/*          cim_defs.h file to be included outside of cimarron.c for    */
/*          basic operations.                                           */
/*                                                                      */
/* Memory Group:                                                        */
/*   Enabling define:                                                   */
/*       CIMARRON_INCLUDE_STRING_MACROS                                 */
/*   Options:                                                           */
/*       CIMARRON_OPTIMIZE_ASSEMBLY                                     */
/*           Set to 1 to allow the use of inline assembly when writing  */
/*           large chunks of data to memory.  Essentially, this allows  */
/*           a rep movsd in place of a slower C for-loop.               */
/*      CIMARRON_OPTIMIZE_FORLOOP                                       */
/*           Define for C only data writes.                             */
/*                                                                      */
/* MSR Group:                                                           */
/*   Enabling define:                                                   */
/*      CIMARRON_INCLUDE_MSR_MACROS                                     */
/*   Options:                                                           */
/*      CIMARRON_MSR_DIRECT_ASM                                         */
/*          Set to 1 to allow the use of the rdmsr and wrmsr opcodes in */
/*          inline assembly.                                            */
/*      CIMARRON_MSR_VSA_IO                                             */
/*          Set to 1 to access MSRs using a VSAII virtual register.     */
/*      CIMARRON_MSR_KERNEL_ROUTINE                                     */
/*          Set to 1 to access MSRs using a wrapper routine in the      */
/*          Linux kernel.                                               */
/*                                                                      */
/* IO Group:                                                            */
/*   Enabling define:                                                   */
/*      CIMARRON_INCLUDE_IO_MACROS                                      */
/*   Options:                                                           */
/*      CIMARRON_IO_DIRECT_ACCESS                                       */
/*          Set to 1 to perform IO accesses using inline assembly.      */
/*      CIMARRON_IO_ABSTRACTED_ASM                                      */
/*          Set to 1 to perform IO using abstracted IO in Linux.        */
/*                                                                      */
/* Custom Group:                                                        */
/*    Disabling define:                                                 */
/*      CIMARRON_EXCLUDE_CUSTOM_MACROS                                  */
/*          By default, the custom macros (the macros used by           */
/*          gp_custom_convert_blt) are mapped to the normal command     */
/*          string macros.  Setting this to 1 allows the user to        */
/*          create a custom implementation.                             */
/*----------------------------------------------------------------------*/

/* UNCOMMENT THE FOLLOWING LINE TO EXCLUDE BASIC REGISTER ACCESS MACROS */

/* #define CIMARRON_EXCLUDE_REGISTER_ACCESS_MACROS */

#define CIMARRON_INCLUDE_STRING_MACROS 
#define CIMARRON_OPTIMIZE_ASSEMBLY         0
#define CIMARRON_OPTIMIZE_FORLOOP          0
#define CIMARRON_OPTIMIZE_ABSTRACTED_ASM   1
  
#define CIMARRON_INCLUDE_MSR_MACROS 
#define CIMARRON_MSR_DIRECT_ASM            0
#define CIMARRON_MSR_VSA_IO                0
#define CIMARRON_MSR_ABSTRACTED_ASM        1
#define CIMARRON_MSR_KERNEL_ROUTINE        0

#define CIMARRON_INCLUDE_IO_MACROS 
#define CIMARRON_IO_DIRECT_ACCESS          0
#define CIMARRON_IO_ABSTRACTED_ASM         1

/* UNCOMMENT THE FOLLOWING LINE TO IMPLEMENT CUSTOM MACROS FOR GP_CUSTOM_CONVERT_BLT */

/* #define CIMARRON_EXCLUDE_CUSTOM_MACROS */

/*----------------------------------------------------------------------*/
/* MODULE VARIABLES                                                     */
/* The following #defines affect how global variables in each Cimarron  */
/* module are defined.  These variables can be made static (to prevent  */
/* naming conflicts) or they can be defined without the static keyword  */
/* (to allow extern references).                                        */
/*----------------------------------------------------------------------*/

#if 1
#define CIMARRON_STATIC static
#else
#define CIMARRON_STATIC
#endif

/*----------------------------------------------------------------------*/
/* CIMARRON GLOBAL VARIABLES                                            */
/* These globals are used by the hardware access macros.  They must be  */
/* initialized by the application to point to the memory-mapped         */
/* registers of their respective blocks.                                */
/*----------------------------------------------------------------------*/

unsigned char *cim_gp_ptr         = (unsigned char *)0;
unsigned char *cim_fb_ptr         = (unsigned char *)0;
unsigned char *cim_cmd_base_ptr   = (unsigned char *)0;
unsigned char *cim_cmd_ptr        = (unsigned char *)0;
unsigned char *cim_vid_ptr        = (unsigned char *)0;
unsigned char *cim_vip_ptr        = (unsigned char *)0;
unsigned char *cim_vg_ptr         = (unsigned char *)0;

/*----------------------------------------------------------------------*/
/* INCLUDE RELEVANT CIMARRON HEADERS                                    */
/*----------------------------------------------------------------------*/

/* HARDWARE REGISTER DEFINITIONS */

#include "cim_regs.h"

/* ROUTINE DEFINITIONS */
/* All routines have a prototype, even those that are not included    */
/* via #ifdefs.  This prevents the user from having to include the    */
/* correct #defines anywhere he/she wants to call a Cimarron routine. */

#include "cim_rtns.h"

/* HARDWARE ACCESS MACROS */

#include "cim_defs.h"

/*----------------------------------------------------------------------*/
/* CIMARRON MODULES                                                     */
/* Modules and sub-modules are included based on user settings.  Note   */
/* that excluding one or more modules may result in functionality       */
/* holes.                                                               */
/*----------------------------------------------------------------------*/

/* GRAPHICS PROCESSOR */

#if CIMARRON_INCLUDE_GP
#include "cim_gp.c"
#endif

/* VIDEO GENERATOR */

#if CIMARRON_INCLUDE_VG
#include "cim_modes.c"
#include "cim_vg.c"
#endif

/* DISPLAY FILTER */

#if CIMARRON_INCLUDE_VIDEO
#include "cim_filter.c"
#include "cim_df.c"
#endif

/* INITIALIZATION AND DETECTION */

#if CIMARRON_INCLUDE_INIT
#include "cim_init.c"
#endif

/* VIP SUPPORT */

#if CIMARRON_INCLUDE_VIP
#include "cim_vip.c"
#endif

/* VOP SUPPORT */

#if CIMARRON_INCLUDE_VOP
#include "cim_vop.c"
#endif

/* MSR ACCESS */
/* This module is used to access machine-specific registers. */
/* It cannot be excluded from a project.                     */

#include "cim_msr.c"

/* LINUX SPECIFIC ADDITIONS */

#include <linux/module.h>

/* cim_msr.c */
EXPORT_SYMBOL(msr_init_table);
EXPORT_SYMBOL(msr_create_geodelink_table);
EXPORT_SYMBOL(msr_create_device_list);
EXPORT_SYMBOL(msr_read64);
EXPORT_SYMBOL(msr_write64);

EXPORT_SYMBOL(cim_inb);
EXPORT_SYMBOL(cim_inw);
EXPORT_SYMBOL(cim_outb);
EXPORT_SYMBOL(cim_outw);

/* cim_df.c */

#if CIMARRON_INCLUDE_VIDEO

EXPORT_SYMBOL(df_set_crt_enable);
EXPORT_SYMBOL(df_set_panel_enable);
EXPORT_SYMBOL(df_configure_video_source);
EXPORT_SYMBOL(df_set_video_offsets);
EXPORT_SYMBOL(df_set_video_scale);
EXPORT_SYMBOL(df_set_video_position);
EXPORT_SYMBOL(df_set_video_filter_coefficients);
EXPORT_SYMBOL(df_set_video_enable);
EXPORT_SYMBOL(df_set_video_color_key);
EXPORT_SYMBOL(df_set_video_palette);
EXPORT_SYMBOL(df_set_video_palette_entry);
EXPORT_SYMBOL(df_configure_video_cursor_color_key);
EXPORT_SYMBOL(df_set_video_cursor_color_key_enable);
EXPORT_SYMBOL(df_configure_alpha_window);
EXPORT_SYMBOL(df_set_alpha_window_enable);
EXPORT_SYMBOL(df_set_no_ck_outside_alpha);
EXPORT_SYMBOL(df_set_video_request);
EXPORT_SYMBOL(df_set_output_color_space);
EXPORT_SYMBOL(df_set_output_path);
EXPORT_SYMBOL(df_test_video_flip_status);
EXPORT_SYMBOL(df_save_state);
EXPORT_SYMBOL(df_restore_state);

#if CIMARRON_INCLUDE_DF_READ_ROUTINES
EXPORT_SYMBOL(df_read_composite_crc);
EXPORT_SYMBOL(df_read_composite_window_crc);
EXPORT_SYMBOL(df_read_panel_crc);
EXPORT_SYMBOL(df_get_video_enable);
EXPORT_SYMBOL(df_get_video_source_configuration);
EXPORT_SYMBOL(df_get_video_position);
EXPORT_SYMBOL(df_get_video_scale);
EXPORT_SYMBOL(df_get_video_filter_coefficients);
EXPORT_SYMBOL(df_get_video_color_key);
EXPORT_SYMBOL(df_get_video_palette_entry);
EXPORT_SYMBOL(df_get_video_palette);
EXPORT_SYMBOL(df_get_video_cursor_color_key);
EXPORT_SYMBOL(df_get_video_cursor_color_key_enable);
EXPORT_SYMBOL(df_get_alpha_window_configuration);
EXPORT_SYMBOL(df_get_alpha_window_enable);
EXPORT_SYMBOL(df_get_video_request);
EXPORT_SYMBOL(df_get_output_color_space);
#endif

#endif

/* cim_gp.c */

#if CIMARRON_INCLUDE_GP

EXPORT_SYMBOL(gp_set_command_buffer_base);
EXPORT_SYMBOL(gp_set_frame_buffer_base);
EXPORT_SYMBOL(gp_set_bpp);
EXPORT_SYMBOL(gp_declare_blt);
EXPORT_SYMBOL(gp_declare_vector);
EXPORT_SYMBOL(gp_write_parameters);
EXPORT_SYMBOL(gp_set_raster_operation);
EXPORT_SYMBOL(gp_set_alpha_operation);
EXPORT_SYMBOL(gp_set_solid_pattern);
EXPORT_SYMBOL(gp_set_mono_pattern);
EXPORT_SYMBOL(gp_set_pattern_origin);
EXPORT_SYMBOL(gp_set_color_pattern);
EXPORT_SYMBOL(gp_set_mono_source);
EXPORT_SYMBOL(gp_set_solid_source);
EXPORT_SYMBOL(gp_set_source_transparency);
EXPORT_SYMBOL(gp_program_lut);
EXPORT_SYMBOL(gp_set_vector_pattern);
EXPORT_SYMBOL(gp_set_strides);
EXPORT_SYMBOL(gp_set_source_format);
EXPORT_SYMBOL(gp_pattern_fill);
EXPORT_SYMBOL(gp_screen_to_screen_blt);
EXPORT_SYMBOL(gp_screen_to_screen_convert);
EXPORT_SYMBOL(gp_color_bitmap_to_screen_blt);
EXPORT_SYMBOL(gp_color_convert_blt);
EXPORT_SYMBOL(gp_custom_convert_blt);
EXPORT_SYMBOL(gp_rotate_blt);
EXPORT_SYMBOL(gp_mono_bitmap_to_screen_blt);
EXPORT_SYMBOL(gp_text_blt);
EXPORT_SYMBOL(gp_mono_expand_blt);
EXPORT_SYMBOL(gp_antialiased_text);
EXPORT_SYMBOL(gp_masked_blt);
EXPORT_SYMBOL(gp_screen_to_screen_masked);
EXPORT_SYMBOL(gp_bresenham_line);
EXPORT_SYMBOL(gp_line_from_endpoints);
EXPORT_SYMBOL(gp_wait_until_idle);
EXPORT_SYMBOL(gp_test_blt_busy);
EXPORT_SYMBOL(gp_test_blt_pending);
EXPORT_SYMBOL(gp_wait_blt_pending);
EXPORT_SYMBOL(gp_save_state);
EXPORT_SYMBOL(gp_restore_state);

#endif

/* cim_init.c */

#if CIMARRON_INCLUDE_INIT

EXPORT_SYMBOL(init_detect_cpu);
EXPORT_SYMBOL(init_read_pci);
EXPORT_SYMBOL(init_read_base_addresses);
EXPORT_SYMBOL(init_read_cpu_frequency);

#endif

/* cim_vg.c */

#if CIMARRON_INCLUDE_VG

EXPORT_SYMBOL(vg_delay_milliseconds);
EXPORT_SYMBOL(vg_set_display_mode);
EXPORT_SYMBOL(vg_set_panel_mode);
EXPORT_SYMBOL(vg_set_tv_mode);
EXPORT_SYMBOL(vg_set_custom_mode);
EXPORT_SYMBOL(vg_get_display_mode_index);
EXPORT_SYMBOL(vg_get_display_mode_information);
EXPORT_SYMBOL(vg_get_display_mode_count);
EXPORT_SYMBOL(vg_get_current_display_mode);
EXPORT_SYMBOL(vg_set_scaler_filter_coefficients);
EXPORT_SYMBOL(vg_configure_flicker_filter);
EXPORT_SYMBOL(vg_set_clock_frequency);
EXPORT_SYMBOL(vg_set_border_color);
EXPORT_SYMBOL(vg_set_cursor_enable);
EXPORT_SYMBOL(vg_set_mono_cursor_colors);
EXPORT_SYMBOL(vg_set_cursor_position);
EXPORT_SYMBOL(vg_set_mono_cursor_shape32);
EXPORT_SYMBOL(vg_set_mono_cursor_shape64);
EXPORT_SYMBOL(vg_set_color_cursor_shape);
EXPORT_SYMBOL(vg_pan_desktop);
EXPORT_SYMBOL(vg_set_display_offset);
EXPORT_SYMBOL(vg_set_display_pitch);
EXPORT_SYMBOL(vg_set_display_palette_entry);
EXPORT_SYMBOL(vg_set_display_palette);
EXPORT_SYMBOL(vg_set_compression_enable);
EXPORT_SYMBOL(vg_configure_compression);
EXPORT_SYMBOL(vg_test_timing_active);
EXPORT_SYMBOL(vg_test_vertical_active);
EXPORT_SYMBOL(vg_wait_vertical_blank);
EXPORT_SYMBOL(vg_configure_line_interrupt);
EXPORT_SYMBOL(vg_test_and_clear_interrupt);
EXPORT_SYMBOL(vg_test_flip_status);
EXPORT_SYMBOL(vg_save_state);
EXPORT_SYMBOL(vg_restore_state);

#if CIMARRON_INCLUDE_VG_READ_ROUTINES
EXPORT_SYMBOL(vg_read_graphics_crc);
EXPORT_SYMBOL(vg_read_window_crc);
EXPORT_SYMBOL(vg_get_scaler_filter_coefficients);
EXPORT_SYMBOL(vg_get_flicker_filter_configuration);
EXPORT_SYMBOL(vg_get_display_pitch);
EXPORT_SYMBOL(vg_get_frame_buffer_line_size);
EXPORT_SYMBOL(vg_get_current_vline);
EXPORT_SYMBOL(vg_get_display_offset);
EXPORT_SYMBOL(vg_get_cursor_info);
EXPORT_SYMBOL(vg_get_display_palette_entry);
EXPORT_SYMBOL(vg_get_border_color);
EXPORT_SYMBOL(vg_get_display_palette);
EXPORT_SYMBOL(vg_get_compression_info);
EXPORT_SYMBOL(vg_get_compression_enable);
EXPORT_SYMBOL(vg_get_valid_bit);
#endif

#endif

#if CIMARRON_INCLUDE_VIP

EXPORT_SYMBOL(vip_initialize);
EXPORT_SYMBOL(vip_update_601_params);
EXPORT_SYMBOL(vip_configure_capture_buffers);
EXPORT_SYMBOL(vip_toggle_video_offsets);
EXPORT_SYMBOL(vip_set_capture_state);
EXPORT_SYMBOL(vip_terminate);
EXPORT_SYMBOL(vip_configure_fifo);
EXPORT_SYMBOL(vip_set_interrupt_enable);
EXPORT_SYMBOL(vip_set_vsync_error);
EXPORT_SYMBOL(vip_max_address_enable);
EXPORT_SYMBOL(vip_set_loopback_enable);
EXPORT_SYMBOL(vip_configure_genlock);
EXPORT_SYMBOL(vip_set_genlock_enable);
EXPORT_SYMBOL(vip_set_power_characteristics);
EXPORT_SYMBOL(vip_set_priority_characteristics);
EXPORT_SYMBOL(vip_set_debug_characteristics);
EXPORT_SYMBOL(vip_configure_pages);
EXPORT_SYMBOL(vip_set_interrupt_line);
EXPORT_SYMBOL(vip_reset);
EXPORT_SYMBOL(vip_set_subwindow_enable);
EXPORT_SYMBOL(vip_reset_interrupt_state);
EXPORT_SYMBOL(vip_save_state);
EXPORT_SYMBOL(vip_restore_state);
EXPORT_SYMBOL(vip_get_interrupt_state);
EXPORT_SYMBOL(vip_test_genlock_active);
EXPORT_SYMBOL(vip_test_signal_status);
EXPORT_SYMBOL(vip_get_current_field);

#if CIMARRON_INCLUDE_VIP_READ_ROUTINES
EXPORT_SYMBOL(vip_get_current_mode);
EXPORT_SYMBOL(vip_get_601_configuration);
EXPORT_SYMBOL(vip_get_buffer_configuration);
EXPORT_SYMBOL(vip_get_genlock_configuration);
EXPORT_SYMBOL(vip_get_genlock_enable);
EXPORT_SYMBOL(vip_is_buffer_update_latched);
EXPORT_SYMBOL(vip_get_capture_state);
EXPORT_SYMBOL(vip_get_current_line);
EXPORT_SYMBOL(vip_read_fifo);
EXPORT_SYMBOL(vip_write_fifo);
EXPORT_SYMBOL(vip_enable_fifo_access);
EXPORT_SYMBOL(vip_get_power_characteristics);
EXPORT_SYMBOL(vip_get_priority_characteristics);
EXPORT_SYMBOL(vip_get_capability_characteristics);
#endif
#endif

#if CIMARRON_INCLUDE_VOP
EXPORT_SYMBOL(vop_set_vbi_window);
EXPORT_SYMBOL(vop_enable_vbi_output);
EXPORT_SYMBOL(vop_set_configuration);
EXPORT_SYMBOL(vop_save_state);
EXPORT_SYMBOL(vop_restore_state);

#if CIMARRON_INCLUDE_VOP_READ_ROUTINES
EXPORT_SYMBOL(vop_get_current_mode);
EXPORT_SYMBOL(vop_get_vbi_configuration);
EXPORT_SYMBOL(vop_get_vbi_enable);
EXPORT_SYMBOL(vop_get_crc);
EXPORT_SYMBOL(vop_read_vbi_crc);
#endif

#endif

/* export memory api */
#include "cim_mem.h"

EXPORT_SYMBOL(cim_get_gp_base);
EXPORT_SYMBOL(cim_get_fb_base);
EXPORT_SYMBOL(cim_get_fb_size);
EXPORT_SYMBOL(cim_get_vg_base);
EXPORT_SYMBOL(cim_get_vid_base);
EXPORT_SYMBOL(cim_get_vip_base);
EXPORT_SYMBOL(cim_get_cmd_size);
EXPORT_SYMBOL(cim_get_cmd_base);
EXPORT_SYMBOL(cim_get_scr_size);
EXPORT_SYMBOL(cim_get_fb_active);

EXPORT_SYMBOL(cim_get_gp_ptr);
EXPORT_SYMBOL(cim_get_fb_ptr);
EXPORT_SYMBOL(cim_get_vg_ptr);
EXPORT_SYMBOL(cim_get_vid_ptr);
EXPORT_SYMBOL(cim_get_vip_ptr);
EXPORT_SYMBOL(cim_get_cmd_base_ptr);
EXPORT_SYMBOL(cim_get_cmd_ptr);


#ifdef MODULE
MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("Geode GX3 Cimarron graphics engine abstraction layer");
MODULE_LICENSE("GPL");
#endif
