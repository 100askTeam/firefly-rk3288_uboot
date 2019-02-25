/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef _ROCKCHIP_CRTC_H_
#define _ROCKCHIP_CRTC_H_

struct rockchip_crtc {
	const struct rockchip_crtc_funcs *funcs;
	const void *data;
	bool hdmi_hpd;
};

struct rockchip_crtc_funcs {
	int (*init)(struct display_state *state);
	void (*deinit)(struct display_state *state);
	int (*set_plane)(struct display_state *state);
	int (*prepare)(struct display_state *state);
	int (*enable)(struct display_state *state);
	int (*disable)(struct display_state *state);
	void (*unprepare)(struct display_state *state);
	int (*fixup_dts)(struct display_state *state, void *blob);
	int (*send_mcu_cmd)(struct display_state *state, u32 type, u32 value);
};

struct vop_data;
extern const struct rockchip_crtc_funcs rockchip_vop_funcs;
extern const struct vop_data rk3036_vop;
extern const struct vop_data px30_vop_lit;
extern const struct vop_data px30_vop_big;
extern const struct vop_data rk3308_vop;
extern const struct vop_data rk3288_vop_big;
extern const struct vop_data rk3288_vop_lit;
extern const struct vop_data rk3368_vop;
extern const struct vop_data rk3366_vop;
extern const struct vop_data rk3399_vop_big;
extern const struct vop_data rk3399_vop_lit;
extern const struct vop_data rk322x_vop;
extern const struct vop_data rk3328_vop;
extern const struct vop_data rv1108_vop;
#endif
