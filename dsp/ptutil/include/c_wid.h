/*
FxSound
Copyright (C) 2025  FxSound LLC

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef _C_WID_H_
#define _C_WID_H_             

#define WID_DISPERSION_MIN_MS 0.5
#define WID_DISPERSION_MAX_MS 25.0
#define WID_CENTER_DEPTH_MIN_MS 0.0
#define WID_CENTER_DEPTH_MAX_MS 25.0

#define WID_FREQ_THRESHOLD_MIN 100.0
#define WID_FREQ_THRESHOLD_MAX 2000.0

#define DSP_WID_INTENSITY_MIN_VALUE 0.0
#define DSP_WID_INTENSITY_MAX_VALUE 1.0

#define DSP_WID_WIDTH_MIN_VALUE 0.0
#define DSP_WID_WIDTH_MAX_VALUE 1.0

#define DSP_WID_REVERSE_WIDTH_MIN_VALUE 1.0
#define DSP_WID_REVERSE_WIDTH_MAX_VALUE 0.0

#define DSP_WID_CENTER_GAIN_MIN_VALUE 0.0
#define DSP_WID_CENTER_GAIN_MAX_VALUE 2.0

#define DSP_WID_STEREO_GAIN_MIN_VALUE 0.0
#define DSP_WID_STEREO_GAIN_MAX_VALUE 0.5

/* Left dispersion is dispersion setting scaled down by this factor */
#define DSP_WID_LEFT_RIGHT_DISPERSION_FACTOR 0.793651

/* First the input parameter locations */
#define DSP_WID_INTENSITY			20L + COMM_MEM_OFFSET
#define DSP_WID_WIDTH				21L + COMM_MEM_OFFSET
#define DSP_WID_REVERSE_WIDTH		22L + COMM_MEM_OFFSET
#define DSP_WID_DISPERSION_LEFT	23L + COMM_MEM_OFFSET
#define DSP_WID_DISPERSION_RIGHT	24L + COMM_MEM_OFFSET
#define DSP_WID_CENTER_GAIN  		25L + COMM_MEM_OFFSET
#define DSP_WID_CENTER_DEPTH 		26L + COMM_MEM_OFFSET
#define DSP_WID_FILTER_GAIN		27L + COMM_MEM_OFFSET
#define DSP_WID_FILTER_A1			28L + COMM_MEM_OFFSET
#define DSP_WID_FILTER_A0			29L + COMM_MEM_OFFSET
#define DSP_WID_BYPASS_FLAG      30L + COMM_MEM_OFFSET

/* Special structure used for parameters and state of algorithm */
struct dspWideStructType
{
	/* Parameters common to all dsp functions */
	/* Note- must occupy same 32 word locations as defines in Boardrv1.h */
	PT_DSP_LONG pc_to_dsp_flags;
	PT_DSP_LONG dsp_to_pc_flags;
	PT_DSP_LONG dsp_number_of_elements;
	realtype dsp_sampling_freq;
	PT_DSP_LONG stereo_in_flag;
	PT_DSP_LONG dsp_mute_in_flag;
	PT_DSP_LONG unassigned6;
	PT_DSP_LONG unassigned7;
	PT_DSP_LONG unassigned8;
	PT_DSP_LONG unassigned9;
	realtype dry_gain;
	realtype wet_gain;
	realtype master_gain;
	PT_DSP_LONG dsp_dma_in_transfer;
	PT_DSP_LONG unassigned14;
	PT_DSP_LONG unassigned15;
	PT_DSP_LONG unassigned16;
	PT_DSP_LONG unassigned17;
	PT_DSP_LONG unassigned18;
	PT_DSP_LONG unassigned19;

	/* Note- algorithm specific parameters must occupy same 32 word locations
	 * as defines above.
	 */
	realtype intensity;
	realtype width;
	realtype reverse_width;
	PT_DSP_LONG dispersion_l;
	PT_DSP_LONG dispersion_r;
	realtype center_gain;
	PT_DSP_LONG center_depth;
	realtype gain;
	realtype a1;
	realtype a0;
	PT_DSP_LONG bypass_flag;

	/* Algorithm state variables */
	realtype *ptr_l;
	realtype *ptr_r;
	realtype *dly_start_l;
	realtype *dly_start_r;
	realtype *ptr_mono;
	realtype *dly_start_mono;

	realtype out1_minus1;
	realtype out1_minus2;
	realtype in1_minus1;
	realtype in1_minus2;
	realtype out2_minus1;
	realtype out2_minus2;
	realtype in2_minus1;
	realtype in2_minus2;
};

#endif
/* _C_WID_H_ */
