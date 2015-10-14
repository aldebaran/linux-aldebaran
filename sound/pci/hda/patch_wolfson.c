/*
 * HD audio interface patch for WM8860
 *
 * Copyright (c) 2015 Aldebaran SoftBank Group <edupin@aldebaran.com>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "hda_generic.h"

#define GPIO_MUTE (0x01)
#define GPIO_MUTE_ENABLE (0x01)
#define GPIO_MUTE_DISABLE (0x00)
#define GPIO_AMPLIFICATION (0x02)
#define GPIO_AMPLIFICATION_ENABLE (0x02)
#define GPIO_AMPLIFICATION_DISABLE (0x00)

#define SINGLE_PIN_WIDGET_ONLY (0xF0)
#define CHANNEL_MONO (0x00)
#define CHANNEL_STEREO (0x01)

#define SET_AMP_STEREO (0x8000 | AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT)

#define VERB_SET_DIFFERENTIAL_MODE (0x7A3)
#define VERB_GET_DIFFERENTIAL_MODE (0xFA3)
/* Availlable Value */
#define DIFFERENTIAL_FULL   (0x00)
#define DIFFERENTIAL_PSEUDO (0x01)


#define NID_PARAMETER (0x01)

#define NID_IO_ADC_1 (0x02)
#define NID_IO_MIC_1 (0x03)
#define NID_IO_MIC_2 (0x15)
#define NID_IO_DAC_1 (0x06)
#define NID_IO_DAC_2 (0x07)

#define NID_WIDGET_MIC_1_MUX (0x09)
#define NID_WIDGET_PGA_1 (0x0A)
#define NID_WIDGET_PGA_2 (0x0B)

#define NID_PORT_E (0x0C)
#define NID_PORT_B (0x0D)
#define NID_PORT_D (0x0E)
#define NID_PORT_H (0x16)
#define NID_PORT_A (0x11)
#define NID_PORT_G (0x12)


struct wm8860_spec {
	struct hda_gen_spec gen;
	/* mixer control */
	const struct snd_kcontrol_new *mixers[20];
	int mixers_count;
	/* Initialization of the codec */
	const struct hda_verb *init_verbs;
	const struct hda_verb *uninit_verbs;
	/* playback */
	unsigned int dac_nids_count;
	const hda_nid_t *dac_nids;
	/* capture */
	unsigned int adc_nids_count;
	const hda_nid_t *adc_nids;
	int mic_1_selection;
	/* capture source */
	const struct hda_input_mux *input_mux;
	const hda_nid_t *capsrc_nids;
	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */
};

/*
 * initialization (common callbacks)
 */
static int wm8860_init(struct hda_codec *codec)
{
	struct wm8860_spec *spec = codec->spec;

	snd_hda_sequence_write(codec, spec->init_verbs);
	return 0;
}

static int wm8860g_build_controls(struct hda_codec *codec)
{
	struct wm8860_spec *spec = codec->spec;
	unsigned int i;
	int err;

	for (i = 0; i < spec->mixers_count; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	return 0;
}

/*
 * Playback callbacks:
 */
static int wm8860_playback_pcm_open(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	struct snd_pcm_substream *substream)
{
	snd_hda_codec_write(codec, NID_PARAMETER, 0, AC_VERB_SET_GPIO_DATA,
		GPIO_MUTE_DISABLE | GPIO_AMPLIFICATION_ENABLE);
	return 0;
}

static int wm8860_playback_pcm_close(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	struct snd_pcm_substream *substream)
{
	snd_hda_codec_write(codec, NID_PARAMETER, 0, AC_VERB_SET_GPIO_DATA,
		GPIO_MUTE_ENABLE | GPIO_AMPLIFICATION_ENABLE);
	return 0;
}

static int wm8860_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	unsigned int stream_tag,
	unsigned int format,
	struct snd_pcm_substream *substream)
{
	struct wm8860_spec *spec = codec->spec;

	snd_hda_codec_write(codec, spec->dac_nids[substream->number], 0,
		AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
	snd_hda_codec_setup_stream(codec, spec->dac_nids[substream->number],
		stream_tag, 0, format);
	return 0;
}

static int wm8860_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	struct snd_pcm_substream *substream)
{
	struct wm8860_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec, spec->dac_nids[substream->number]);
	snd_hda_codec_write(codec, spec->dac_nids[substream->number], 0,
		AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
	return 0;
}

/*
 * Capture callbacks:
 */
static int wm8860_capture_pcm_open(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	struct snd_pcm_substream *substream)
{
	struct wm8860_spec *spec = codec->spec;

	if (spec->adc_nids[substream->number] != NID_IO_MIC_1)
		return 0;
	if (spec->mic_1_selection < 0) {
		spec->mic_1_selection = substream->number;
		if (spec->capsrc_nids[substream->number] == NID_PORT_B)
			snd_hda_codec_write(codec, NID_WIDGET_MIC_1_MUX, 0,
				AC_VERB_SET_CONNECT_SEL, 0x00);
		else
			snd_hda_codec_write(codec, NID_WIDGET_MIC_1_MUX, 0,
				AC_VERB_SET_CONNECT_SEL, 0x01);
		return 0;
	}
	snd_printk(KERN_WARNING
		"Try to open a channel already open in Numeric/Analogic\n");
	return -EBUSY;

}

static int wm8860_capture_pcm_close(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	struct snd_pcm_substream *substream)
{
	struct wm8860_spec *spec = codec->spec;

	if (spec->adc_nids[substream->number] != NID_IO_MIC_1)
		return 0;
	if (spec->mic_1_selection == substream->number) {
		spec->mic_1_selection = -1;
		return 0;
	}
	snd_printk(KERN_WARNING
		"Try to close a stream with Numeric/Analogic error\n");
	return -EFAULT;
}

static int wm8860_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	unsigned int stream_tag,
	unsigned int format,
	struct snd_pcm_substream *substream)
{
	struct wm8860_spec *spec = codec->spec;

	snd_hda_codec_write(codec, spec->adc_nids[substream->number], 0,
		AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
		stream_tag, 0, format);
	return 0;
}

static int wm8860_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
	struct hda_codec *codec,
	struct snd_pcm_substream *substream)
{
	struct wm8860_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	snd_hda_codec_write(codec, spec->adc_nids[substream->number], 0,
		AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
	return 0;
}

static const struct hda_pcm_stream wm8860_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0,
	.ops = {
		.open = wm8860_playback_pcm_open,
		.prepare = wm8860_playback_pcm_prepare,
		.cleanup = wm8860_playback_pcm_cleanup,
		.close = wm8860_playback_pcm_close,
	},
};

static const struct hda_pcm_stream wm8860_pcm_analog_capture = {
	.substreams = 4,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0,
	.ops = {
		.open = wm8860_capture_pcm_open,
		.prepare = wm8860_capture_pcm_prepare,
		.cleanup = wm8860_capture_pcm_cleanup,
		.close = wm8860_capture_pcm_close,
	},
};

static int wm8860_build_pcms(struct hda_codec *codec)
{
	struct wm8860_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;
	info->name = "wm8860";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = wm8860_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->dac_nids_count;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dac_nids[0];
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = wm8860_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
		spec->adc_nids_count;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];
	return 0;
}

static void wm8860_shutup(struct hda_codec *codec)
{
	snd_hda_shutup_pins(codec);
	snd_hda_codec_write(codec, NID_PARAMETER, 0, AC_VERB_SET_GPIO_DATA,
		GPIO_MUTE_ENABLE | GPIO_AMPLIFICATION_DISABLE);
}

static void wm8860_free(struct hda_codec *codec)
{
	struct wm8860_spec *spec = codec->spec;

	if (!spec)
		return;
	snd_hda_sequence_write(codec, spec->uninit_verbs);
	snd_hda_gen_spec_free(&spec->gen);
	kfree(spec);
	snd_hda_detach_beep_device(codec);
}

static struct hda_codec_ops wm8860g_patch_ops = {
	.build_controls = wm8860g_build_controls,
	.build_pcms = wm8860_build_pcms,
	.init = wm8860_init,
	.free = wm8860_free,
};

/*
 * Automatic parse of I/O pins from the BIOS configuration
 */
static int wm8860_auto_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	return 0;
}

static const struct hda_codec_ops wm8860_auto_patch_ops = {
	.build_controls = wm8860_auto_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
	.reboot_notify = wm8860_shutup,
};

static hda_nid_t wm8860g_dac_nids[] = {
	NID_IO_DAC_2, NID_IO_DAC_1
};

static hda_nid_t wm8860g_adc_nids[] = {
	NID_IO_MIC_1, NID_IO_ADC_1, NID_IO_MIC_1, NID_IO_MIC_2
};

static hda_nid_t wm8860g_capsrc_nids[] = {
	NID_PORT_B, NID_PORT_E, NID_PORT_D, NID_PORT_H
};

static struct hda_input_mux wm8860g_capture_source = {
	.num_items = 4,
	.items = {
		{ "Analog Rear", NID_IO_MIC_1 }, /* port-B */
		{ "Analog Front", NID_IO_ADC_1 }, /* port-E */
		{ "Numeric Left", NID_IO_MIC_1 }, /* port-B */
		{ "Numeric Right", NID_IO_ADC_1 }, /* port-E */
	},
};

static struct snd_kcontrol_new wm8860g_playback_mixers[] = {
	HDA_CODEC_VOLUME("Analog Front Playback Volume", NID_IO_DAC_2,
		0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Analog Rear Playback Volume", NID_IO_DAC_1,
		0x0, HDA_OUTPUT),
	{ /* end */ }
};

/* capture */
static struct snd_kcontrol_new wm8860g_capture_mixers[] = {
	/*
	 * Analog section:
	 */
	HDA_CODEC_VOLUME("Analog Rear mics Capture Volume", NID_WIDGET_PGA_2,
		0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Analog Rear mics Capture Switch", NID_WIDGET_PGA_2,
		0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Analog Front mics Capture Volume", NID_WIDGET_PGA_1,
		0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Analog Front mics Capture Switch", NID_WIDGET_PGA_1,
		0x0, HDA_OUTPUT),
	/*
	 * Numeric section:
	 */
	HDA_CODEC_VOLUME("Numeric Left mics Capture Volume", NID_PORT_D,
		0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Numeric Left mics Capture Switch", NID_PORT_D,
		0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Numeric Right mics Capture Volume", NID_PORT_H,
		0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Numeric Right mics Capture Switch", NID_PORT_H,
		0x0, HDA_INPUT),
	{ /* end */ }
};

static const struct hda_verb wm8860g_capture_init_verbs[] = {
	/* ---------------------------------
	 * reset codec to have all the time the good configuration:
	 * Note: the RESET is a command ==> no value needed
	 * --------------------------------- */
	{NID_PARAMETER, AC_VERB_SET_CODEC_RESET, 0},
	/* ---------------------------------
	 * Set all output and input to mute before configuring device
	 *---------------------------------*/
	/* Mute All Input */
	{NID_WIDGET_PGA_1, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | AC_AMP_MUTE},
	{NID_WIDGET_PGA_2, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | AC_AMP_MUTE},
	/* mute input port: */
	{NID_WIDGET_PGA_1, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | AC_AMP_MUTE},
	{NID_WIDGET_PGA_2, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | AC_AMP_MUTE},
	/* mute Output port: */
	{NID_IO_DAC_1, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | AC_AMP_MUTE},
	{NID_IO_DAC_2, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | AC_AMP_MUTE},
	/* ---------------------------------
	 * Configure ADC-1
	 * --------------------------------- */
	{NID_IO_ADC_1, AC_VERB_SET_STREAM_FORMAT,
		  AC_FMT_BASE_48K
		| (0 << AC_FMT_MULT_SHIFT)
		| (0 << AC_FMT_DIV_SHIFT)
		| AC_FMT_BITS_16
		| (CHANNEL_STEREO << AC_FMT_CHAN_SHIFT)},
	/* ---------------------------------
	 * Configure MIC-1
	 * --------------------------------- */
	{NID_IO_MIC_1, AC_VERB_SET_STREAM_FORMAT,
		  AC_FMT_BASE_48K
		| (0 << AC_FMT_MULT_SHIFT)
		| (0 << AC_FMT_DIV_SHIFT)
		| AC_FMT_BITS_16
		| (CHANNEL_STEREO << AC_FMT_CHAN_SHIFT)},
	/* ---------------------------------
	 * Configure MIC-2
	 * --------------------------------- */
	{NID_IO_MIC_2, AC_VERB_SET_STREAM_FORMAT,
		  AC_FMT_BASE_48K
		| (0 << AC_FMT_MULT_SHIFT)
		| (0 << AC_FMT_DIV_SHIFT)
		| AC_FMT_BITS_16
		| (CHANNEL_STEREO << AC_FMT_CHAN_SHIFT)},
	/* ---------------------------------
	 * Configure DAC-1
	 * --------------------------------- */
	{NID_IO_DAC_1, AC_VERB_SET_STREAM_FORMAT,
		  AC_FMT_BASE_48K
		| (0 << AC_FMT_MULT_SHIFT)
		| (0 << AC_FMT_DIV_SHIFT)
		| AC_FMT_BITS_16
		| (CHANNEL_STEREO << AC_FMT_CHAN_SHIFT)},
	/* ---------------------------------
	 * Configure DAC-2
	 * --------------------------------- */
	{NID_IO_DAC_2, AC_VERB_SET_STREAM_FORMAT,
		  AC_FMT_BASE_48K
		| (0 << AC_FMT_MULT_SHIFT)
		| (0 << AC_FMT_DIV_SHIFT)
		| AC_FMT_BITS_16
		| (CHANNEL_STEREO << AC_FMT_CHAN_SHIFT)},
	/* ---------------------------------
	 * configure Port-E specification:
	 * --------------------------------- */
	{NID_PORT_E, AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
		AC_JACK_LOC_EXTERNAL | AC_JACK_LOC_FRONT},
	{NID_PORT_E, AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
		(AC_JACK_MIC_IN<<8) | AC_JACK_CONN_UNKNOWN},
	{NID_PORT_E, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
		(AC_JACK_COLOR_UNKNOWN<<8) | AC_DEFCFG_MISC_NO_PRESENCE},
	{NID_PORT_E, AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
		SINGLE_PIN_WIDGET_ONLY},
	/* ---------------------------------
	 * Configure port B specification
	 * --------------------------------- */
	{NID_PORT_B, AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
		AC_JACK_LOC_EXTERNAL | AC_JACK_LOC_REAR},
	{NID_PORT_B, AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
		(AC_JACK_MIC_IN<<8) | AC_JACK_CONN_UNKNOWN},
	{NID_PORT_B, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
		(AC_JACK_COLOR_UNKNOWN<<8) | AC_DEFCFG_MISC_NO_PRESENCE},
	{NID_PORT_B, AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
		SINGLE_PIN_WIDGET_ONLY},
	{NID_PORT_B, VERB_SET_DIFFERENTIAL_MODE,
		DIFFERENTIAL_FULL},
	/* ---------------------------------
	 * Configure port D specification
	 * --------------------------------- */
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
		AC_JACK_LOC_EXTERNAL | AC_JACK_LOC_LEFT},
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
		(AC_JACK_MIC_IN<<8) | AC_JACK_CONN_UNKNOWN},
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
		(AC_JACK_COLOR_UNKNOWN<<8) | AC_DEFCFG_MISC_NO_PRESENCE},
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
		SINGLE_PIN_WIDGET_ONLY},
	/* ---------------------------------
	 * Configure port H specification
	 * --------------------------------- */
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
		AC_JACK_LOC_EXTERNAL | AC_JACK_LOC_RIGHT},
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
		(AC_JACK_MIC_IN<<8) | AC_JACK_CONN_UNKNOWN},
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
		(AC_JACK_COLOR_UNKNOWN<<8) | AC_DEFCFG_MISC_NO_PRESENCE},
	{NID_PORT_D, AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
		SINGLE_PIN_WIDGET_ONLY},
	/* ---------------------------------
	 * GPIO configuration
	 * --------------------------------- */
	/* Set GPIO in manual mode (specific WM88XX): */
	{NID_PARAMETER, 0x786, 0x00},
	{NID_PARAMETER, AC_VERB_SET_GPIO_MASK,
		GPIO_MUTE | GPIO_AMPLIFICATION},
	{NID_PARAMETER, AC_VERB_SET_GPIO_DIRECTION,
		GPIO_MUTE | GPIO_AMPLIFICATION},
	{NID_PARAMETER, AC_VERB_SET_GPIO_DATA,
		GPIO_MUTE_ENABLE | GPIO_AMPLIFICATION_ENABLE},
	/* ---------------------------------
	 * configuration done ... unmute neededs
	 * --------------------------------- */
	/* Un-mute input port: and set basic volume at 0 dB = 0x18*/
	{NID_WIDGET_PGA_1, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | 0x18},
	{NID_WIDGET_PGA_2, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | 0x18},
	{NID_PORT_D, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | 0x18},
	{NID_PORT_H, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | 0x18},
	/* Un-mute Output port: set at 0x48 correct basic volume */
	{NID_IO_DAC_2, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | 0x48},
	{NID_IO_DAC_1, AC_VERB_SET_AMP_GAIN_MUTE,
		SET_AMP_STEREO | 0x48},
	/* ---------------------------------
	 * Start Streams (all time, no need to change state)
	 * --------------------------------- */
	/* Enables DAC and ADC: */
	{NID_IO_DAC_1, AC_VERB_SET_POWER_STATE, AC_PWRST_D3},
	{NID_IO_DAC_2, AC_VERB_SET_POWER_STATE, AC_PWRST_D3},
	{NID_IO_ADC_1, AC_VERB_SET_POWER_STATE, AC_PWRST_D3},
	{NID_IO_MIC_1, AC_VERB_SET_POWER_STATE, AC_PWRST_D3},
	{NID_IO_MIC_2, AC_VERB_SET_POWER_STATE, AC_PWRST_D3},
	/* Authorise the codec to start all IOs */
	{NID_PARAMETER, AC_VERB_SET_POWER_STATE, AC_PWRST_D0},
	{ /* end */ }
};

static const struct hda_verb wm8860g_capture_uninit_verbs[] = {
	{NID_PARAMETER, AC_VERB_SET_GPIO_DATA,
		GPIO_MUTE_ENABLE | GPIO_AMPLIFICATION_DISABLE},

};

static int patch_wm8860g(struct hda_codec *codec)
{
	struct wm8860_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;
	/* set new allocated data */
	codec->spec = spec;
	/* set basic conficuration of the codec: */
	spec->init_verbs = wm8860g_capture_init_verbs;
	spec->uninit_verbs = wm8860g_capture_uninit_verbs;
	spec->mixers_count = 0;
	/* Configure outputs: */
	spec->dac_nids_count = ARRAY_SIZE(wm8860g_dac_nids);
	spec->dac_nids = wm8860g_dac_nids;
	spec->mixers[spec->mixers_count++] = wm8860g_playback_mixers;
	/* configure Inputs: */
	spec->adc_nids_count = ARRAY_SIZE(wm8860g_adc_nids);
	spec->adc_nids = wm8860g_adc_nids;
	spec->mic_1_selection = -1;
	spec->mixers[spec->mixers_count++] = wm8860g_capture_mixers;
	spec->input_mux = &wm8860g_capture_source;
	spec->capsrc_nids = wm8860g_capsrc_nids;
	codec->patch_ops = wm8860g_patch_ops;
	codec->no_trigger_sense = 1;
	return 0;
}

/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_wolfson[] = {
	{ .id = 0x1aec8800, .name = "WM8860", .patch = patch_wm8860g },
	{ /* end */ }
};

MODULE_ALIAS("snd-hda-codec-id:1aec8800");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wolfson Multimedia HD-audio codec");

static struct hda_codec_preset_list wolfson_list = {
	.preset = snd_hda_preset_wolfson,
	.owner = THIS_MODULE,
};

static int __init patch_wolfson_init(void)
{
	return snd_hda_add_codec_preset(&wolfson_list);
}

static void __exit patch_wolfson_exit(void)
{
	snd_hda_delete_codec_preset(&wolfson_list);
}

module_init(patch_wolfson_init)
module_exit(patch_wolfson_exit)

