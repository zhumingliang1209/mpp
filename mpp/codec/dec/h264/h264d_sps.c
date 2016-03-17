/*
*
* Copyright 2015 Rockchip Electronics Co. LTD
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#define   MODULE_TAG  "h264d_sps"

#include <string.h>

#include "mpp_mem.h"

#include "vpu_api.h"

#include "h264d_log.h"
#include "h264d_sps.h"
#include "h264d_scalist.h"
#include "h264d_dpb.h"

static void reset_cur_sps_data(H264_SPS_t *cur_sps)
{
    memset(cur_sps, 0, sizeof(H264_SPS_t));
    cur_sps->seq_parameter_set_id = 0;  // reset
}

static void reset_cur_subpps_data(H264_subSPS_t *cur_subpps)
{
    memset(cur_subpps, 0, sizeof(H264_subSPS_t));
    cur_subpps->sps.seq_parameter_set_id = 0;  // reset
    cur_subpps->num_views_minus1 = -1;
    cur_subpps->num_level_values_signalled_minus1 = -1;
}

static MPP_RET read_hrd_parameters(BitReadCtx_t *p_bitctx, H264_HRD_t *hrd)
{
    RK_U32 SchedSelIdx = 0;
    MPP_RET ret = MPP_ERR_UNKNOW;

    READ_UE(p_bitctx, &hrd->cpb_cnt_minus1, "cpb_cnt_minus1");
    READ_BITS(p_bitctx, 4, &hrd->bit_rate_scale, "bit_rate_scale");
    READ_BITS(p_bitctx, 4, &hrd->cpb_size_scale, "cpb_size_scale");
    for (SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
        READ_UE(p_bitctx, &hrd->bit_rate_value_minus1[SchedSelIdx], "VUI: bit_rate_value_minus1");
        READ_UE(p_bitctx, &hrd->cpb_size_value_minus1[SchedSelIdx], "VUI: cpb_size_value_minus1");
        READ_ONEBIT(p_bitctx, &hrd->cbr_flag[SchedSelIdx], "VUI: cbr_flag");
    }
    READ_BITS(p_bitctx, 5, &hrd->initial_cpb_removal_delay_length_minus1, "initial_cpb_removal_delay_length_minus1");
    READ_BITS(p_bitctx, 5, &hrd->cpb_removal_delay_length_minus1, "cpb_removal_delay_length_minus1");
    READ_BITS(p_bitctx, 5, &hrd->dpb_output_delay_length_minus1, "dpb_output_delay_length_minus1");
    READ_BITS(p_bitctx, 5, &hrd->time_offset_length, "time_offset_length");

    return ret = MPP_OK;
__BITREAD_ERR:
    return ret = p_bitctx->ret;
}

static void init_VUI(H264_VUI_t *vui)
{
    vui->matrix_coefficients = 2;
}

static MPP_RET read_VUI(BitReadCtx_t *p_bitctx, H264_VUI_t *vui)
{
    MPP_RET ret = MPP_ERR_UNKNOW;

    READ_ONEBIT(p_bitctx, &vui->aspect_ratio_info_present_flag, "vui.aspect_ratio_info_present_flag");
    if (vui->aspect_ratio_info_present_flag) {
        READ_BITS(p_bitctx, 8, &vui->aspect_ratio_idc, "vui.aspect_ratio_idc");
        if (255 == vui->aspect_ratio_idc) {
            READ_BITS(p_bitctx, 16, &vui->sar_width, "VUI: sar_width");
            READ_BITS(p_bitctx, 16, &vui->sar_height, "VUI: sar_height");
        }
    }
    READ_ONEBIT(p_bitctx, &vui->overscan_info_present_flag, "VUI: overscan_info_present_flag");
    if (vui->overscan_info_present_flag) {
        READ_ONEBIT(p_bitctx, &vui->overscan_appropriate_flag, "VUI: overscan_appropriate_flag");
    }
    READ_ONEBIT(p_bitctx, &vui->video_signal_type_present_flag, "VUI: video_signal_type_present_flag");
    if (vui->video_signal_type_present_flag) {
        READ_BITS(p_bitctx, 3, &vui->video_format, "VUI: video_format");
        READ_ONEBIT(p_bitctx, &vui->video_full_range_flag, "VUI: video_full_range_flag");
        READ_ONEBIT(p_bitctx, &vui->colour_description_present_flag, "VUI: color_description_present_flag");
        if (vui->colour_description_present_flag) {
            READ_BITS(p_bitctx, 8, &vui->colour_primaries, "VUI: colour_primaries");
            READ_BITS(p_bitctx, 8, &vui->transfer_characteristics, "VUI: transfer_characteristics");
            READ_BITS(p_bitctx, 8, &vui->matrix_coefficients, "VUI: matrix_coefficients");
        }
    }
    READ_ONEBIT(p_bitctx, &vui->chroma_location_info_present_flag, "VUI: chroma_location_info_present_flag");
    if (vui->chroma_location_info_present_flag) {
        READ_UE(p_bitctx, &vui->chroma_sample_loc_type_top_field, "VUI: chroma_sample_loc_type_top_field");
        READ_UE(p_bitctx, &vui->chroma_sample_loc_type_bottom_field, "VUI: chroma_sample_loc_type_bottom_field");
    }
    READ_ONEBIT(p_bitctx, &vui->timing_info_present_flag, "VUI: timing_info_present_flag");
    if (vui->timing_info_present_flag) {
        READ_BITS(p_bitctx, 16, &vui->num_units_in_tick, "VUI: num_units_in_tick(high 16bit)");
        READ_BITS(p_bitctx, 16, &vui->num_units_in_tick, "VUI: num_units_in_tick(low  16bit)");
        READ_BITS(p_bitctx, 16, &vui->time_scale, "VUI: time_scale(high 16bit)");
        READ_BITS(p_bitctx, 16, &vui->time_scale, "VUI: time_scale(low  16bit)");
        READ_ONEBIT(p_bitctx, &vui->fixed_frame_rate_flag, "VUI: fixed_frame_rate_flag");
    }
    READ_ONEBIT(p_bitctx, &vui->nal_hrd_parameters_present_flag, "VUI: nal_hrd_parameters_present_flag");
    if (vui->nal_hrd_parameters_present_flag) {
        FUN_CHECK(ret = read_hrd_parameters(p_bitctx, &vui->nal_hrd_parameters));
    }
    READ_ONEBIT(p_bitctx, &vui->vcl_hrd_parameters_present_flag, "VUI: vcl_hrd_parameters_present_flag");
    if (vui->vcl_hrd_parameters_present_flag) {
        FUN_CHECK(ret = read_hrd_parameters(p_bitctx, &vui->vcl_hrd_parameters));
    }
    if (vui->nal_hrd_parameters_present_flag || vui->vcl_hrd_parameters_present_flag) {
        READ_ONEBIT(p_bitctx, &vui->low_delay_hrd_flag, "VUI: low_delay_hrd_flag");
    }
    READ_ONEBIT(p_bitctx, &vui->pic_struct_present_flag, "VUI: pic_struct_present_flag");
    READ_ONEBIT(p_bitctx, &vui->bitstream_restriction_flag, "VUI: bitstream_restriction_flag");
    if (vui->bitstream_restriction_flag) {
        READ_ONEBIT(p_bitctx, &vui->motion_vectors_over_pic_boundaries_flag, "VUI: motion_vectors_over_pic_boundaries_flag");
        READ_UE(p_bitctx, &vui->max_bytes_per_pic_denom, "VUI: max_bytes_per_pic_denom");
        READ_UE(p_bitctx, &vui->max_bits_per_mb_denom, "VUI: max_bits_per_mb_denom");
        READ_UE(p_bitctx, &vui->log2_max_mv_length_horizontal, "VUI: log2_max_mv_length_horizontal");
        READ_UE(p_bitctx, &vui->log2_max_mv_length_vertical, "VUI: log2_max_mv_length_vertical");
        READ_UE(p_bitctx, &vui->num_reorder_frames, "VUI: num_reorder_frames");
        READ_UE(p_bitctx, &vui->max_dec_frame_buffering, "VUI: max_dec_frame_buffering");
    }

    return ret = MPP_OK;
__BITREAD_ERR:
    ret = p_bitctx->ret;
__FAILED:
    return ret;
}

static MPP_RET parser_sps(BitReadCtx_t *p_bitctx, H264_SPS_t *cur_sps, H264_DecCtx_t *p_Dec)
{
    RK_S32 i = 0, temp = 0;
    MPP_RET ret = MPP_ERR_UNKNOW;
    //!< init Fidelity Range Extensions stuff
    cur_sps->chroma_format_idc       = 1;
    cur_sps->bit_depth_luma_minus8   = 0;
    cur_sps->bit_depth_chroma_minus8 = 0;
    cur_sps->qpprime_y_zero_transform_bypass_flag = 0;
    cur_sps->separate_colour_plane_flag           = 0;
    cur_sps->log2_max_pic_order_cnt_lsb_minus4    = 0;
    cur_sps->delta_pic_order_always_zero_flag     = 0;
    LogInfo(p_bitctx->ctx, "----------------------------- SPS begin --------------------------------");
    READ_BITS(p_bitctx, 8, &cur_sps->profile_idc, "profile_idc");
    VAL_CHECK (ret, (cur_sps->profile_idc == BASELINE)  || (cur_sps->profile_idc == H264_MAIN)
               || (cur_sps->profile_idc == EXTENDED)    || (cur_sps->profile_idc == FREXT_HP)
               || (cur_sps->profile_idc == FREXT_Hi10P) || (cur_sps->profile_idc == FREXT_Hi422)
               || (cur_sps->profile_idc == FREXT_Hi444) || (cur_sps->profile_idc == FREXT_CAVLC444)
               || (cur_sps->profile_idc == MVC_HIGH)    || (cur_sps->profile_idc == STEREO_HIGH)
              );
    READ_ONEBIT(p_bitctx, &cur_sps->constrained_set0_flag, "constrained_set0_flag");
    READ_ONEBIT(p_bitctx, &cur_sps->constrained_set1_flag, "constrained_set1_flag");
    READ_ONEBIT(p_bitctx, &cur_sps->constrained_set2_flag, "constrained_set2_flag");
    READ_ONEBIT(p_bitctx, &cur_sps->constrained_set3_flag, "constrained_set3_flag");
    READ_ONEBIT(p_bitctx, &cur_sps->constrained_set4_flag, "constrained_set4_flag");
    READ_ONEBIT(p_bitctx, &cur_sps->constrained_set5_flag, "constrained_set5_flag");
    READ_BITS(p_bitctx, 2, &temp, "reserved_zero_2bits");  // reserved_zero_2bits
    ASSERT(temp == 0);
    READ_BITS(p_bitctx, 8, &cur_sps->level_idc, "level_idc");
    READ_UE(p_bitctx, &cur_sps->seq_parameter_set_id, "seq_parameter_set_id");
	if (cur_sps->seq_parameter_set_id <0 || cur_sps->seq_parameter_set_id > 32)	{
		cur_sps->seq_parameter_set_id = 0;
	}
    if (cur_sps->profile_idc == 100 || cur_sps->profile_idc == 110
        || cur_sps->profile_idc == 122 || cur_sps->profile_idc == 244
        || cur_sps->profile_idc == 44  || cur_sps->profile_idc == 83
        || cur_sps->profile_idc == 86  || cur_sps->profile_idc == 118
        || cur_sps->profile_idc == 128 || cur_sps->profile_idc == 138) {
        READ_UE(p_bitctx, &cur_sps->chroma_format_idc, "chroma_format_idc");
        ASSERT(cur_sps->chroma_format_idc < 4);
        //if (cur_sps->chroma_format_idc >= 3) {
        //  H264D_ERR("ERROR: Not support chroma_format_idc=%d.", cur_sps->chroma_format_idc);
        //    p_Dec->err_ctx.err_flag |= VPU_FRAME_ERR_UNSUPPORT;
        //  goto __FAILED;
        //}
        //if (cur_sps->chroma_format_idc == 3) {
        //    READ_ONEBIT(p_bitctx, &cur_sps->separate_colour_plane_flag, "separate_colour_plane_flag");
        //    LogError(p_bitctx->ctx, "Not support YUV444 format current.");
        //}
        READ_UE(p_bitctx, &cur_sps->bit_depth_luma_minus8, "bit_depth_luma_minus8");
        ASSERT(cur_sps->bit_depth_luma_minus8 < 7);
        READ_UE(p_bitctx, &cur_sps->bit_depth_chroma_minus8, "bit_depth_chroma_minus8");
        ASSERT(cur_sps->bit_depth_chroma_minus8 < 7);
        READ_ONEBIT(p_bitctx, &cur_sps->qpprime_y_zero_transform_bypass_flag, "qpprime_y_zero_transform_bypass_flag");
        READ_ONEBIT(p_bitctx, &cur_sps->seq_scaling_matrix_present_flag, "seq_scaling_matrix_present_flag");
        if (cur_sps->seq_scaling_matrix_present_flag) {
            LogInfo(p_bitctx->ctx, "Scaling matrix present.");
            if (parse_sps_scalinglists(p_bitctx, cur_sps)) {
                LogError(p_bitctx->ctx, "rkv_parse_sps_scalinglists error.");
            }
        }
    }
    READ_UE(p_bitctx, &cur_sps->log2_max_frame_num_minus4, "log2_max_frame_num_minus4");
    ASSERT(cur_sps->log2_max_frame_num_minus4 < 13);
    READ_UE(p_bitctx, &cur_sps->pic_order_cnt_type, "pic_order_cnt_type");
    ASSERT(cur_sps->pic_order_cnt_type < 3);

    cur_sps->log2_max_pic_order_cnt_lsb_minus4 = 0;
    cur_sps->delta_pic_order_always_zero_flag = 0;
    if (0 == cur_sps->pic_order_cnt_type) {
        READ_UE(p_bitctx, &cur_sps->log2_max_pic_order_cnt_lsb_minus4, "log2_max_pic_order_cnt_lsb_minus4");
        ASSERT(cur_sps->log2_max_pic_order_cnt_lsb_minus4 < 13);
    } else if (1 == cur_sps->pic_order_cnt_type) {
        READ_ONEBIT(p_bitctx, &cur_sps->delta_pic_order_always_zero_flag, "delta_pic_order_always_zero_flag");
        READ_SE(p_bitctx, &cur_sps->offset_for_non_ref_pic, "offset_for_non_ref_pic");
        READ_SE(p_bitctx, &cur_sps->offset_for_top_to_bottom_field, "offset_for_top_to_bottom_field");
        READ_UE(p_bitctx, &cur_sps->num_ref_frames_in_pic_order_cnt_cycle, "num_ref_frames_in_pic_order_cnt_cycle");
        ASSERT(cur_sps->num_ref_frames_in_pic_order_cnt_cycle < 256);
        for (i = 0; i < cur_sps->num_ref_frames_in_pic_order_cnt_cycle; ++i) {
            READ_SE(p_bitctx, &cur_sps->offset_for_ref_frame[i], "offset_for_ref_frame");
            cur_sps->expected_delta_per_pic_order_cnt_cycle += cur_sps->offset_for_ref_frame[i];
        }
    }
    READ_UE(p_bitctx, &cur_sps->max_num_ref_frames, "max_num_ref_frames");
    READ_ONEBIT(p_bitctx, &cur_sps->gaps_in_frame_num_value_allowed_flag, "gaps_in_frame_num_value_allowed_flag");
    READ_UE(p_bitctx, &cur_sps->pic_width_in_mbs_minus1, "pic_width_in_mbs_minus1");
    READ_UE(p_bitctx, &cur_sps->pic_height_in_map_units_minus1, "pic_height_in_map_units_minus1");
    READ_ONEBIT(p_bitctx, &cur_sps->frame_mbs_only_flag, "frame_mbs_only_flag");
    if (!cur_sps->frame_mbs_only_flag) {
        READ_ONEBIT(p_bitctx, &cur_sps->mb_adaptive_frame_field_flag, "mb_adaptive_frame_field_flag");
    }
    READ_ONEBIT(p_bitctx, &cur_sps->direct_8x8_inference_flag, "direct_8x8_inference_flag");

    READ_ONEBIT(p_bitctx, &cur_sps->frame_cropping_flag, "frame_cropping_flag");
    if (cur_sps->frame_cropping_flag) {
        READ_UE(p_bitctx, &cur_sps->frame_crop_left_offset, "frame_crop_left_offset");
        READ_UE(p_bitctx, &cur_sps->frame_crop_right_offset, "frame_crop_right_offset");
        READ_UE(p_bitctx, &cur_sps->frame_crop_top_offset, "frame_crop_top_offset");
        READ_UE(p_bitctx, &cur_sps->frame_crop_bottom_offset, "frame_crop_bottom_offset");
    }
    READ_ONEBIT(p_bitctx, &cur_sps->vui_parameters_present_flag, "vui_parameters_present_flag");

    init_VUI(&cur_sps->vui_seq_parameters);
    if (cur_sps->vui_parameters_present_flag) {
        FUN_CHECK(ret = read_VUI(p_bitctx, &cur_sps->vui_seq_parameters));
    }
    cur_sps->Valid = 1;
	(void)p_Dec;

    return ret = MPP_OK;
__BITREAD_ERR:
    ret = p_bitctx->ret;
__FAILED:
    return ret;
}

static MPP_RET sps_mvc_extension(BitReadCtx_t *p_bitctx, H264_subSPS_t *subset_sps)
{
    MPP_RET ret = MPP_ERR_UNKNOW;
    RK_S32 i = 0, j = 0, num_views = 0;

    READ_UE(p_bitctx, &subset_sps->num_views_minus1, "SPS_MVC_EXT:num_views_minus1");
    num_views = 1 + subset_sps->num_views_minus1;
    //========================
    if (num_views > 0) {
        subset_sps->view_id                = mpp_calloc(RK_S32,  num_views);
        subset_sps->num_anchor_refs_l0     = mpp_calloc(RK_S32,  num_views);
        subset_sps->num_anchor_refs_l1     = mpp_calloc(RK_S32,  num_views);
        subset_sps->anchor_ref_l0          = mpp_calloc(RK_S32*, num_views);
        subset_sps->anchor_ref_l1          = mpp_calloc(RK_S32*, num_views);
        subset_sps->num_non_anchor_refs_l0 = mpp_calloc(RK_S32,  num_views);
        subset_sps->num_non_anchor_refs_l1 = mpp_calloc(RK_S32,  num_views);
        subset_sps->non_anchor_ref_l0      = mpp_calloc(RK_S32*, num_views);
        subset_sps->non_anchor_ref_l1      = mpp_calloc(RK_S32*, num_views);
        MEM_CHECK(ret, subset_sps->view_id        && subset_sps->num_anchor_refs_l0
                  && subset_sps->num_anchor_refs_l1     && subset_sps->anchor_ref_l0
                  && subset_sps->anchor_ref_l1          && subset_sps->num_non_anchor_refs_l0
                  && subset_sps->num_non_anchor_refs_l1 && subset_sps->non_anchor_ref_l0
                  && subset_sps->non_anchor_ref_l1);
    }
    for (i = 0; i < num_views; i++) {
        READ_UE(p_bitctx, &subset_sps->view_id[i], "SPS_MVC_EXT: view_id");
    }
    for (i = 1; i < num_views; i++) {
        READ_UE(p_bitctx, &subset_sps->num_anchor_refs_l0[i], "SPS_MVC_EXT: num_anchor_refs_l0");
        if (subset_sps->num_anchor_refs_l0[i]) {
            subset_sps->anchor_ref_l0[i] = mpp_calloc(RK_S32, subset_sps->num_anchor_refs_l0[i]);
            MEM_CHECK(ret, subset_sps->anchor_ref_l0[i]);
            for (j = 0; j < subset_sps->num_anchor_refs_l0[i]; j++) {
                READ_UE(p_bitctx, &subset_sps->anchor_ref_l0[i][j], "SPS_MVC_EXT: anchor_ref_l0");
            }
        }
        READ_UE(p_bitctx, &subset_sps->num_anchor_refs_l1[i], "SPS_MVC_EXT: num_anchor_refs_l1");
        if (subset_sps->num_anchor_refs_l1[i]) {
            subset_sps->anchor_ref_l1[i] = mpp_calloc(RK_S32, subset_sps->num_anchor_refs_l1[i]);
            MEM_CHECK(ret, subset_sps->anchor_ref_l1[i]);
            for (j = 0; j < subset_sps->num_anchor_refs_l1[i]; j++) {
                READ_UE(p_bitctx, &subset_sps->anchor_ref_l1[i][j], "SPS_MVC_EXT: anchor_ref_l0");
            }
        }
    }
    for (i = 1; i < num_views; i++) {
        READ_UE(p_bitctx, &subset_sps->num_non_anchor_refs_l0[i], "SPS_MVC_EXT: num_non_anchor_refs_l0");
        if (subset_sps->num_non_anchor_refs_l0[i]) {
            subset_sps->non_anchor_ref_l0[i] = mpp_calloc(RK_S32, subset_sps->num_non_anchor_refs_l0[i]);
            MEM_CHECK(ret, subset_sps->non_anchor_ref_l0[i]);
            for (j = 0; j < subset_sps->num_non_anchor_refs_l0[i]; j++) {
                READ_UE(p_bitctx, &subset_sps->non_anchor_ref_l0[i][j], "SPS_MVC_EXT: non_anchor_ref_l0");
            }
        }
        READ_UE(p_bitctx, &subset_sps->num_non_anchor_refs_l1[i], "SPS_MVC_EXT: num_non_anchor_refs_l1");
        if (subset_sps->num_non_anchor_refs_l1[i]) {
            subset_sps->non_anchor_ref_l1[i] = mpp_calloc(RK_S32, subset_sps->num_non_anchor_refs_l1[i]);
            MEM_CHECK(ret, subset_sps->non_anchor_ref_l1[i]);

            for (j = 0; j < subset_sps->num_non_anchor_refs_l1[i]; j++) {
                READ_UE(p_bitctx, &subset_sps->non_anchor_ref_l1[i][j], "SPS_MVC_EXT: non_anchor_ref_l1");
            }
        }
    }
    return ret = MPP_OK;
__BITREAD_ERR:
    ret = p_bitctx->ret;
__FAILED:
    return ret;
}

static MPP_RET parser_subsps_ext(BitReadCtx_t *p_bitctx, H264_subSPS_t *cur_subsps)
{
    MPP_RET ret = MPP_ERR_UNKNOW;

    if ((cur_subsps->sps.profile_idc == MVC_HIGH)
        || (cur_subsps->sps.profile_idc == STEREO_HIGH)) {
        READ_ONEBIT(p_bitctx, &cur_subsps->bit_equal_to_one, "bit_equal_to_one");
        ASSERT(cur_subsps->bit_equal_to_one == 1);
        FUN_CHECK(ret = sps_mvc_extension(p_bitctx, cur_subsps));

        READ_ONEBIT(p_bitctx, &cur_subsps->mvc_vui_parameters_present_flag, "mvc_vui_parameters_present_flag");
    }

    return ret = MPP_OK;
__BITREAD_ERR:
    ret = p_bitctx->ret;
__FAILED:
    return ret;
}

static void update_video_pars(H264dVideoCtx_t *p_Vid, H264_SPS_t *sps)
{
    RK_U32 crop_left = 0, crop_right = 0;
    RK_U32 crop_top = 0, crop_bottom = 0;
    static const RK_U32 SubWidthC  [4] = { 1, 2, 2, 1};
    static const RK_U32 SubHeightC [4] = { 1, 2, 1, 1};


    p_Vid->max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);
    p_Vid->PicWidthInMbs = (sps->pic_width_in_mbs_minus1 + 1);
    p_Vid->FrameHeightInMbs = (2 - sps->frame_mbs_only_flag) * (sps->pic_height_in_map_units_minus1 + 1);
    p_Vid->yuv_format = sps->chroma_format_idc;

    p_Vid->width = p_Vid->PicWidthInMbs * 16;
    p_Vid->height = p_Vid->FrameHeightInMbs * 16;
    p_Vid->bit_depth_luma = sps->bit_depth_luma_minus8 + 8;
    p_Vid->bit_depth_chroma = sps->bit_depth_chroma_minus8 + 8;

    if (p_Vid->yuv_format == YUV420) {
        p_Vid->width_cr = (p_Vid->width >> 1);
        p_Vid->height_cr = (p_Vid->height >> 1);
    } else if (p_Vid->yuv_format == YUV422) {
        p_Vid->width_cr = (p_Vid->width >> 1);
        p_Vid->height_cr = p_Vid->height;
    }
    //!< calculate frame_width_after_crop && frame_height_after_crop
    if (sps->frame_cropping_flag)   {
        crop_left   = SubWidthC [sps->chroma_format_idc] * sps->frame_crop_left_offset;
        crop_right  = SubWidthC [sps->chroma_format_idc] * sps->frame_crop_right_offset;
        crop_top    = SubHeightC[sps->chroma_format_idc] * ( 2 - sps->frame_mbs_only_flag ) * sps->frame_crop_top_offset;
        crop_bottom = SubHeightC[sps->chroma_format_idc] * ( 2 - sps->frame_mbs_only_flag ) * sps->frame_crop_bottom_offset;
    } else {
        crop_left = crop_right = crop_top = crop_bottom = 0;
    }
    p_Vid->width_after_crop = p_Vid->width - crop_left - crop_right;
    p_Vid->height_after_crop = p_Vid->height - crop_top - crop_bottom;
}

static RK_U32 video_pars_changed(H264dVideoCtx_t *p_Vid, H264_SPS_t *sps, RK_U8 layer_id)
{
    RK_U32 ret = 0;

    ret |= p_Vid->p_Dpb_layer[layer_id]->num_ref_frames != sps->max_num_ref_frames;
    ret |= p_Vid->last_pic_width_in_mbs_minus1[layer_id] != sps->pic_width_in_mbs_minus1;
    ret |= p_Vid->last_pic_height_in_map_units_minus1[layer_id] != sps->pic_height_in_map_units_minus1;
    ret |= p_Vid->last_profile_idc[layer_id] != sps->profile_idc;
    ret |= p_Vid->last_level_idc[layer_id] != sps->level_idc;
    ret |= !p_Vid->p_Dpb_layer[layer_id]->init_done;

    return ret;
}

static void update_last_video_pars(H264dVideoCtx_t *p_Vid, H264_SPS_t *sps, RK_U8 layer_id)
{
    p_Vid->last_pic_width_in_mbs_minus1[layer_id] = sps->pic_width_in_mbs_minus1;
    p_Vid->last_pic_height_in_map_units_minus1[layer_id] = sps->pic_height_in_map_units_minus1;
    p_Vid->last_profile_idc[layer_id] = sps->profile_idc;
    p_Vid->last_level_idc[layer_id] = sps->level_idc;
}

/*!
***********************************************************************
* \brief
*    prase sps and process sps
***********************************************************************
*/
//extern "C"
MPP_RET process_sps(H264_SLICE_t *currSlice)
{
    MPP_RET ret = MPP_ERR_UNKNOW;
    H264dLogCtx_t *logctx = currSlice->logctx;
    H264dCurCtx_t *p_Cur = currSlice->p_Cur;
    BitReadCtx_t *p_bitctx = &p_Cur->bitctx;
    H264_SPS_t *cur_sps = &p_Cur->sps;

    FunctionIn(logctx->parr[RUN_PARSE]);
    reset_cur_sps_data(cur_sps); // reset
    set_bitread_logctx(p_bitctx, logctx->parr[LOG_READ_SPS]);
    //!< parse sps
    FUN_CHECK(ret = parser_sps(p_bitctx, cur_sps, currSlice->p_Dec));
    //!< decide "max_dec_frame_buffering" for DPB
    FUN_CHECK(ret = get_max_dec_frame_buf_size(cur_sps));
    //!< make SPS available, copy
    if (cur_sps->Valid) {
        memcpy(&currSlice->p_Vid->spsSet[cur_sps->seq_parameter_set_id], cur_sps, sizeof(H264_SPS_t));
    }
    FunctionOut(logctx->parr[RUN_PARSE]);

    return ret = MPP_OK;
__FAILED:
    return ret;
}


/*!
***********************************************************************
* \brief
*    prase sps and process sps
***********************************************************************
*/
//extern "C"
void recycle_subsps(H264_subSPS_t *subset_sps)
{
    RK_S32 i = 0, num_views = 0;

    num_views = 1 + subset_sps->num_views_minus1;
    for (i = 1; i < num_views; i++) {
        if (subset_sps->num_anchor_refs_l0[i] > 0) {
            MPP_FREE(subset_sps->anchor_ref_l0[i]);
        }
        if (subset_sps->num_anchor_refs_l1[i] > 0) {
            MPP_FREE(subset_sps->anchor_ref_l1[i]);
        }
        if (subset_sps->num_non_anchor_refs_l0[i] > 0) {
            MPP_FREE(subset_sps->non_anchor_ref_l0[i]);
        }
        if (subset_sps->num_non_anchor_refs_l1[i] > 0) {
            MPP_FREE(subset_sps->non_anchor_ref_l1[i]);
        }
    }
    if (num_views > 0) {
        MPP_FREE(subset_sps->view_id);
        MPP_FREE(subset_sps->num_anchor_refs_l0);
        MPP_FREE(subset_sps->num_anchor_refs_l1);
        MPP_FREE(subset_sps->anchor_ref_l0);
        MPP_FREE(subset_sps->anchor_ref_l1);
        MPP_FREE(subset_sps->num_non_anchor_refs_l0);
        MPP_FREE(subset_sps->num_non_anchor_refs_l1);
        MPP_FREE(subset_sps->non_anchor_ref_l0);
        MPP_FREE(subset_sps->non_anchor_ref_l1);
    }
    subset_sps->Valid = 0;
}

/*!
***********************************************************************
* \brief
*    prase sps and process sps
***********************************************************************
*/
//extern "C"
MPP_RET process_subsps(H264_SLICE_t *currSlice)
{
    MPP_RET ret = MPP_ERR_UNKNOW;
    H264dLogCtx_t *logctx = currSlice->logctx;
    BitReadCtx_t *p_bitctx = &currSlice->p_Cur->bitctx;
    H264_subSPS_t *cur_subsps = &currSlice->p_Cur->subsps;
    H264_subSPS_t *p_subset = NULL;

    FunctionIn(logctx->parr[RUN_PARSE]);
    reset_cur_subpps_data(cur_subsps); //reset
    set_bitread_logctx(p_bitctx, logctx->parr[LOG_READ_SUBSPS]);
    LogInfo(p_bitctx->ctx, "----------------------------- subSPS begin --------------------------------");
    FUN_CHECK(ret = parser_sps(p_bitctx, &cur_subsps->sps, currSlice->p_Dec));
    FUN_CHECK(ret = parser_subsps_ext(p_bitctx, cur_subsps));
    if (cur_subsps->sps.Valid) {
        cur_subsps->Valid = 1;
        currSlice->p_Vid->profile_idc = cur_subsps->sps.profile_idc;
    }
    get_max_dec_frame_buf_size(&cur_subsps->sps);
    //!< make subSPS available
    p_subset = &currSlice->p_Vid->subspsSet[cur_subsps->sps.seq_parameter_set_id];
    if (p_subset->Valid) {
        recycle_subsps(p_subset);
    }
    memcpy(p_subset, cur_subsps, sizeof(H264_subSPS_t));
    FunctionOut(logctx->parr[RUN_PARSE]);

    return ret = MPP_OK;
__FAILED:
    recycle_subsps(&currSlice->p_Cur->subsps);
    //ASSERT(0);

    return ret;
}

/*!
***********************************************************************
* \brief
*    prase sps and process sps
***********************************************************************
*/
//extern "C"
MPP_RET activate_sps(H264dVideoCtx_t *p_Vid, H264_SPS_t *sps, H264_subSPS_t *subset_sps)
{
    MPP_RET ret = MPP_ERR_UNKNOW;
	INP_CHECK(ret, !p_Vid && !sps && !subset_sps);
    if (p_Vid->dec_pic) {
        FUN_CHECK(ret = exit_picture(p_Vid, &p_Vid->dec_pic));
    }
    if (p_Vid->active_mvc_sps_flag) { // layer_id == 1
        p_Vid->active_sps = &subset_sps->sps;
        p_Vid->active_subsps = subset_sps;
        p_Vid->active_sps_id[0] = 0;
        p_Vid->active_sps_id[1] = subset_sps->sps.seq_parameter_set_id;
		VAL_CHECK(ret, subset_sps->sps.seq_parameter_set_id >= 0);
        if (video_pars_changed(p_Vid, p_Vid->active_sps, 1)) {
            FUN_CHECK(ret = flush_dpb(p_Vid->p_Dpb_layer[1], 2));
            FUN_CHECK(ret = init_dpb(p_Vid, p_Vid->p_Dpb_layer[1], 2));
            update_last_video_pars(p_Vid, p_Vid->active_sps, 1);
            //!< init frame slots, store frame buffer size
            p_Vid->dpb_size[1] = p_Vid->p_Dpb_layer[1]->size;
        }
		VAL_CHECK(ret, p_Vid->dpb_size[1] > 0);
    } else { //!< layer_id == 0
        p_Vid->active_sps = sps;
        p_Vid->active_subsps = NULL;		
		VAL_CHECK(ret, sps->seq_parameter_set_id >= 0);
        p_Vid->active_sps_id[0] = sps->seq_parameter_set_id;
        p_Vid->active_sps_id[1] = 0;
        if (video_pars_changed(p_Vid, p_Vid->active_sps, 0)) {
            if (!p_Vid->no_output_of_prior_pics_flag) {
                FUN_CHECK(ret = flush_dpb(p_Vid->p_Dpb_layer[0], 1));
            }
            FUN_CHECK(ret = init_dpb(p_Vid, p_Vid->p_Dpb_layer[0], 1));
            update_last_video_pars(p_Vid, p_Vid->active_sps, 0);
            //!< init frame slots, store frame buffer size
            p_Vid->dpb_size[0] = p_Vid->p_Dpb_layer[0]->size;
        }
		VAL_CHECK(ret, p_Vid->dpb_size[0] > 0);
    }
	H264D_DBG(H264D_DBG_DPB_INFO, "[DPB size] dpb_size[0]=%d, mvc_flag=%d, dpb_size[1]=%d", 
		p_Vid->dpb_size[0], p_Vid->active_mvc_sps_flag, p_Vid->dpb_size[1]);
    update_video_pars(p_Vid, p_Vid->active_sps);
__RETURN:
    return ret = MPP_OK;
__FAILED:
    return ret;
}
