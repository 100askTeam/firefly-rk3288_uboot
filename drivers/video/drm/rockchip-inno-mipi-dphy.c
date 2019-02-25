/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <asm/unaligned.h>
#include <asm/io.h>
#include <linux/list.h>
#include <div64.h>

#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_phy.h"
#include "rockchip_mipi_dsi.h"

/* Innosilicon MIPI D-PHY registers */
#define INNO_PHY_LANE_CTRL	0x00000
#define MIPI_BGPD		BIT(7)
#define CLK_LANE_EN_MASK	BIT(6)
#define DATA_LANE_3_EN_MASK	BIT(5)
#define DATA_LANE_2_EN_MASK	BIT(4)
#define DATA_LANE_1_EN_MASK	BIT(3)
#define DATA_LANE_0_EN_MASK	BIT(2)
#define CLK_LANE_EN		BIT(6)
#define DATA_LANE_3_EN		BIT(5)
#define DATA_LANE_2_EN		BIT(4)
#define DATA_LANE_1_EN		BIT(3)
#define DATA_LANE_0_EN		BIT(2)
#define PWROK_BP		BIT(1)
#define PWROK			BIT(0)
#define INNO_PHY_POWER_CTRL	0x00004
#define ANALOG_RESET_MASK	BIT(2)
#define ANALOG_RESET		BIT(2)
#define ANALOG_NORMAL		0
#define LDO_POWER_MASK		BIT(1)
#define LDO_POWER_DOWN		BIT(1)
#define LDO_POWER_ON		0
#define PLL_POWER_MASK		BIT(0)
#define PLL_POWER_DOWN		BIT(0)
#define PLL_POWER_ON		0
#define INNO_PHY_PLL_CTRL_0	0x0000c
#define FBDIV_HI_MASK		BIT(5)
#define FBDIV_HI(x)		UPDATE(x, 5, 5)
#define PREDIV_MASK		GENMASK(4, 0)
#define PREDIV(x)		UPDATE(x, 4, 0)
#define INNO_PHY_PLL_CTRL_1	0x00010
#define FBDIV_LO_MASK		GENMASK(7, 0)
#define FBDIV_LO(x)		UPDATE(x, 7, 0)
#define INNO_PHY_DIG_CTRL	0x00080
#define DIGITAL_RESET_MASK	BIT(0)
#define DIGITAL_NORMAL		BIT(0)
#define DIGITAL_RESET		0
#define INNO_PHY_LVDS_CTRL	0x003ac
#define LVDS_BGPD		BIT(0)

#define INNO_CLOCK_LANE_REG_BASE	0x00100
#define INNO_DATA_LANE_0_REG_BASE	0x00180
#define INNO_DATA_LANE_1_REG_BASE	0x00200
#define INNO_DATA_LANE_2_REG_BASE	0x00280
#define INNO_DATA_LANE_3_REG_BASE	0x00300

#define T_LPX_OFFSET		0x00014
#define T_HS_PREPARE_OFFSET	0x00018
#define T_HS_ZERO_OFFSET	0x0001c
#define T_HS_TRAIL_OFFSET	0x00020
#define T_HS_EXIT_OFFSET	0x00024
#define T_CLK_POST_OFFSET	0x00028
#define T_WAKUP_H_OFFSET	0x00030
#define T_WAKUP_L_OFFSET	0x00034
#define T_CLK_PRE_OFFSET	0x00038
#define T_TA_GO_OFFSET		0x00040
#define T_TA_SURE_OFFSET	0x00044
#define T_TA_WAIT_OFFSET	0x00048

#define T_LPX_MASK		GENMASK(5, 0)
#define T_LPX(x)		UPDATE(x, 5, 0)
#define T_HS_PREPARE_MASK	GENMASK(6, 0)
#define T_HS_PREPARE(x)		UPDATE(x, 6, 0)
#define T_HS_ZERO_MASK		GENMASK(5, 0)
#define T_HS_ZERO(x)		UPDATE(x, 5, 0)
#define T_HS_TRAIL_MASK		GENMASK(6, 0)
#define T_HS_TRAIL(x)		UPDATE(x, 6, 0)
#define T_HS_EXIT_MASK		GENMASK(4, 0)
#define T_HS_EXIT(x)		UPDATE(x, 4, 0)
#define T_CLK_POST_MASK		GENMASK(3, 0)
#define T_CLK_POST(x)		UPDATE(x, 3, 0)
#define T_WAKUP_H_MASK		GENMASK(1, 0)
#define T_WAKUP_H(x)		UPDATE(x, 1, 0)
#define T_WAKUP_L_MASK		GENMASK(7, 0)
#define T_WAKUP_L(x)		UPDATE(x, 7, 0)
#define T_CLK_PRE_MASK		GENMASK(3, 0)
#define T_CLK_PRE(x)		UPDATE(x, 3, 0)
#define T_TA_GO_MASK		GENMASK(5, 0)
#define T_TA_GO(x)		UPDATE(x, 5, 0)
#define T_TA_SURE_MASK		GENMASK(5, 0)
#define T_TA_SURE(x)		UPDATE(x, 5, 0)
#define T_TA_WAIT_MASK		GENMASK(5, 0)
#define T_TA_WAIT(x)		UPDATE(x, 5, 0)

enum lane_type {
	CLOCK_LANE,
	DATA_LANE_0,
	DATA_LANE_1,
	DATA_LANE_2,
	DATA_LANE_3,
};

struct inno_mipi_dphy_timing {
	u8 lpx;
	u8 hs_prepare;
	u8 hs_zero;
	u8 hs_trail;
	u8 hs_exit;
	u8 clk_post;
	u8 wakup_h;
	u8 wakup_l;
	u8 clk_pre;
	u8 ta_go;
	u8 ta_sure;
	u8 ta_wait;
};

struct inno_mipi_dphy {
	const void *blob;
	ofnode node;
	void __iomem *regs;

	unsigned int lane_mbps;
	int lanes;
	int bpp;
};

static const u32 lane_reg_offset[] = {
	[CLOCK_LANE] = INNO_CLOCK_LANE_REG_BASE,
	[DATA_LANE_0] = INNO_DATA_LANE_0_REG_BASE,
	[DATA_LANE_1] = INNO_DATA_LANE_1_REG_BASE,
	[DATA_LANE_2] = INNO_DATA_LANE_2_REG_BASE,
	[DATA_LANE_3] = INNO_DATA_LANE_3_REG_BASE,
};

#define FIXED_PARAM(_freq, _prepare, _clk_zero, _data_zero, _trail)	\
{	\
	.max_freq = _freq,	\
	.hs_prepare = _prepare,	\
	.clk_lane = {	\
		.hs_zero = _clk_zero,	\
	},	\
	.data_lane = {	\
		.hs_zero = _data_zero,	\
	},	\
	.hs_trail = _trail,	\
}

struct fixed_param {
	unsigned int max_freq;
	u8 hs_prepare;
	struct {
		u8 hs_zero;
	} clk_lane;
	struct {
		u8 hs_zero;
	} data_lane;
	u8 hs_trail;
};

static const struct fixed_param fixed_param_table[] = {
	FIXED_PARAM( 110, 0x20, 0x16, 0x02, 0x22),
	FIXED_PARAM( 150, 0x06, 0x16, 0x03, 0x45),
	FIXED_PARAM( 200, 0x18, 0x17, 0x04, 0x0b),
	FIXED_PARAM( 250, 0x05, 0x17, 0x05, 0x16),
	FIXED_PARAM( 300, 0x51, 0x18, 0x06, 0x2c),
	FIXED_PARAM( 400, 0x64, 0x19, 0x07, 0x33),
	FIXED_PARAM( 500, 0x20, 0x1b, 0x07, 0x4e),
	FIXED_PARAM( 600, 0x6a, 0x1d, 0x08, 0x3a),
	FIXED_PARAM( 700, 0x3e, 0x1e, 0x08, 0x6a),
	FIXED_PARAM( 800, 0x21, 0x1f, 0x09, 0x29),
	FIXED_PARAM(1000, 0x09, 0x20, 0x09, 0x27)
};

static inline void inno_write(struct inno_mipi_dphy *inno, u32 reg, u32 val)
{
	writel(val, inno->regs + reg);
}

static inline u32 inno_read(struct inno_mipi_dphy *inno, u32 reg)
{
	return readl(inno->regs + reg);
}

static inline void inno_update_bits(struct inno_mipi_dphy *inno, u32 reg,
				    u32 mask, u32 val)
{
	u32 tmp, orig;

	orig = inno_read(inno, reg);
	tmp = orig & ~mask;
	tmp |= val & mask;
	inno_write(inno, reg, tmp);
}

static void inno_mipi_dphy_timing_update(struct inno_mipi_dphy *inno,
					 enum lane_type lane_type,
					 struct inno_mipi_dphy_timing *t)
{
	u32 base = lane_reg_offset[lane_type];
	u32 m, v;

	m = T_HS_PREPARE_MASK;
	v = T_HS_PREPARE(t->hs_prepare);
	inno_update_bits(inno, base + T_HS_PREPARE_OFFSET, m, v);

	m = T_HS_ZERO_MASK;
	v = T_HS_ZERO(t->hs_zero);
	inno_update_bits(inno, base + T_HS_ZERO_OFFSET, m, v);

	m = T_HS_TRAIL_MASK;
	v = T_HS_TRAIL(t->hs_trail);
	inno_update_bits(inno, base + T_HS_TRAIL_OFFSET, m, v);

	m = T_HS_EXIT_MASK;
	v = T_HS_EXIT(t->hs_exit);
	inno_update_bits(inno, base + T_HS_EXIT_OFFSET, m, v);

	if (lane_type == CLOCK_LANE) {
		m = T_CLK_POST_MASK;
		v = T_CLK_POST(t->clk_post);
		inno_update_bits(inno, base + T_CLK_POST_OFFSET, m, v);

		m = T_CLK_PRE_MASK;
		v = T_CLK_PRE(t->clk_pre);
		inno_update_bits(inno, base + T_CLK_PRE_OFFSET, m, v);
	}

	m = T_WAKUP_H_MASK;
	v = T_WAKUP_H(t->wakup_h);
	inno_update_bits(inno, base + T_WAKUP_H_OFFSET, m, v);

	m = T_WAKUP_L_MASK;
	v = T_WAKUP_L(t->wakup_l);
	inno_update_bits(inno, base + T_WAKUP_L_OFFSET, m, v);

	m = T_LPX_MASK;
	v = T_LPX(t->lpx);
	inno_update_bits(inno, base + T_LPX_OFFSET, m, v);

	m = T_TA_GO_MASK;
	v = T_TA_GO(t->ta_go);
	inno_update_bits(inno, base + T_TA_GO_OFFSET, m, v);

	m = T_TA_SURE_MASK;
	v = T_TA_SURE(t->ta_sure);
	inno_update_bits(inno, base + T_TA_SURE_OFFSET, m, v);

	m = T_TA_WAIT_MASK;
	v = T_TA_WAIT(t->ta_wait);
	inno_update_bits(inno, base + T_TA_WAIT_OFFSET, m, v);
}

static void inno_mipi_dphy_get_fixed_param(struct inno_mipi_dphy_timing *t,
					   unsigned int freq,
					   enum lane_type lane_type)
{
	const struct fixed_param *param;
	int i;

	for (i = 0; i < ARRAY_SIZE(fixed_param_table); i++)
		if (freq <= fixed_param_table[i].max_freq)
			break;

	if (i == ARRAY_SIZE(fixed_param_table))
		--i;

	param = &fixed_param_table[i];

	if (lane_type == CLOCK_LANE)
		t->hs_zero = param->clk_lane.hs_zero;
	else
		t->hs_zero = param->data_lane.hs_zero;

	t->hs_prepare = param->hs_prepare;
	t->hs_trail = param->hs_trail;
}

static void inno_mipi_dphy_lane_timing_init(struct inno_mipi_dphy *inno,
					    enum lane_type lane_type)
{
	struct mipi_dphy_timing timing;
	struct inno_mipi_dphy_timing data;
	unsigned long txbyteclk, txclkesc, UI;
	unsigned int esc_clk_div;

	memset(&timing, 0, sizeof(timing));
	memset(&data, 0, sizeof(data));

	txbyteclk = inno->lane_mbps * USEC_PER_SEC / 8;
	esc_clk_div = DIV_ROUND_UP(txbyteclk, 20000000);
	txclkesc = txbyteclk / esc_clk_div;
	UI = DIV_ROUND_CLOSEST(NSEC_PER_USEC, inno->lane_mbps);

	debug("txbyteclk=%lu, txclkesc=%lu, esc_clk_div=%u, UI=%lu\n",
	      txbyteclk, txclkesc, esc_clk_div, UI);

	mipi_dphy_timing_get_default(&timing, UI);
	inno_mipi_dphy_get_fixed_param(&data, inno->lane_mbps, lane_type);

	/*
	 * Ttxbyteclk * val >= Ths-exit
	 * Ttxbyteclk * val >= Tclk-post
	 * Ttxbyteclk * val >= Tclk-pre
	 * Ttxbyteclk * (2 + val) >= Tlpx
	 */
	data.hs_exit = DIV_ROUND_UP(timing.hsexit * txbyteclk, NSEC_PER_SEC);
	data.clk_post = DIV_ROUND_UP(timing.clkpost * txbyteclk, NSEC_PER_SEC);
	data.clk_pre = DIV_ROUND_UP(timing.clkpre * txbyteclk, NSEC_PER_SEC);
	data.wakup_h = 0x3;
	data.wakup_l = 0xff;
	data.lpx = DIV_ROUND_UP(txbyteclk * timing.lpx, NSEC_PER_SEC);
	if (data.lpx > 2)
		data.lpx -= 2;

	/*
	 * Ttxclkesc * val >= Tta-go
	 * Ttxclkesc * val >= Tta-sure
	 * Ttxclkesc * val >= Tta-wait
	 */
	data.ta_go = DIV_ROUND_UP(timing.tago * txclkesc, NSEC_PER_SEC);
	data.ta_sure = DIV_ROUND_UP(timing.tasure * txclkesc, NSEC_PER_SEC);
	data.ta_wait = DIV_ROUND_UP(timing.taget * txclkesc, NSEC_PER_SEC);

	inno_mipi_dphy_timing_update(inno, lane_type, &data);

#define TIMING_NS(x, freq) (((x) * (DIV_ROUND_CLOSEST(NSEC_PER_SEC, freq))))
	debug("hs-exit=%lu, clk-post=%lu, clk-pre=%lu, lpx=%lu\n",
	      TIMING_NS(data.hs_exit, txbyteclk),
	      TIMING_NS(data.clk_post, txbyteclk),
	      TIMING_NS(data.clk_pre, txbyteclk),
	      TIMING_NS(data.lpx + 2, txbyteclk));
	debug("ta-go=%lu, ta-sure=%lu, ta-wait=%lu\n",
	      TIMING_NS(data.ta_go, txclkesc),
	      TIMING_NS(data.ta_sure, txclkesc),
	      TIMING_NS(data.ta_wait, txclkesc));
}

static unsigned long inno_mipi_dphy_pll_round_rate(unsigned long fin,
						   unsigned long fout,
						   u8 *prediv, u16 *fbdiv)
{
	unsigned long best_freq = 0;
	u8 min_prediv, max_prediv;
	u8 _prediv, best_prediv = 0;
	u16 _fbdiv, best_fbdiv = 0;
	u32 min_delta = 0xffffffff;

	fout *= 2;

	min_prediv = DIV_ROUND_UP(fin, 40000000);
	max_prediv = fin / 5000000;

	for (_prediv = min_prediv; _prediv <= max_prediv; _prediv++) {
		u64 tmp;
		u32 delta;
		tmp = (u64)fout * _prediv;
		do_div(tmp, fin);
		_fbdiv = tmp;
		if ((_fbdiv == 15) || (_fbdiv < 12) || (_fbdiv > 511))
			continue;
		tmp = (u64)_fbdiv * fin;
		do_div(tmp, _prediv);

		delta = abs(fout - tmp);
		if (delta < min_delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			min_delta = delta;
			best_freq = tmp;
		}
	}

	if (best_freq) {
		*prediv = best_prediv;
		*fbdiv = best_fbdiv;
	}

	return best_freq / 2;
}

static inline void inno_mipi_dphy_reset(struct inno_mipi_dphy *inno)
{
	/* Reset analog */
	inno_update_bits(inno, INNO_PHY_POWER_CTRL,
			 ANALOG_RESET_MASK, ANALOG_RESET);
	udelay(1);
	inno_update_bits(inno, INNO_PHY_POWER_CTRL,
			 ANALOG_RESET_MASK, ANALOG_NORMAL);
	/* Reset digital */
	inno_update_bits(inno, INNO_PHY_DIG_CTRL,
			 DIGITAL_RESET_MASK, DIGITAL_RESET);
	udelay(1);
	inno_update_bits(inno, INNO_PHY_DIG_CTRL,
			 DIGITAL_RESET_MASK, DIGITAL_NORMAL);
}

static void inno_mipi_dphy_timing_init(struct inno_mipi_dphy *inno)
{
	switch (inno->lanes) {
	case 4:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_3);
		/* Fall through */
	case 3:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_2);
		/* Fall through */
	case 2:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_1);
		/* Fall through */
	case 1:
	default:
		inno_mipi_dphy_lane_timing_init(inno, DATA_LANE_0);
		inno_mipi_dphy_lane_timing_init(inno, CLOCK_LANE);
		break;
	}
}

static inline void inno_mipi_dphy_lane_enable(struct inno_mipi_dphy *inno)
{
	u32 m = 0, v = 0;

	switch (inno->lanes) {
	case 4:
		m |= DATA_LANE_3_EN_MASK;
		v |= DATA_LANE_3_EN;
		/* Fall through */
	case 3:
		m |= DATA_LANE_2_EN_MASK;
		v |= DATA_LANE_2_EN;
		/* Fall through */
	case 2:
		m |= DATA_LANE_1_EN_MASK;
		v |= DATA_LANE_1_EN;
		/* Fall through */
	default:
	case 1:
		m |= DATA_LANE_0_EN_MASK | CLK_LANE_EN_MASK;
		v |= DATA_LANE_0_EN | CLK_LANE_EN;
		break;
	}

	inno_update_bits(inno, INNO_PHY_LANE_CTRL, m, v);
}

static inline void inno_mipi_dphy_pll_ldo_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_POWER_CTRL,
			 PLL_POWER_MASK | LDO_POWER_MASK,
			 PLL_POWER_DOWN | LDO_POWER_DOWN);
}

static inline void inno_mipi_dphy_pll_ldo_enable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_POWER_CTRL,
			 PLL_POWER_MASK | LDO_POWER_MASK,
			 PLL_POWER_ON | LDO_POWER_ON);
}

static inline void inno_mipi_dphy_da_pwrok_enable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_LANE_CTRL, PWROK_BP | PWROK, PWROK);
}

static inline void inno_mipi_dphy_da_pwrok_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_LANE_CTRL, PWROK_BP | PWROK, PWROK_BP);
}

static inline void inno_mipi_dphy_bgpd_enable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_LANE_CTRL, MIPI_BGPD, 0);
}

static inline void inno_mipi_dphy_bgpd_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_LANE_CTRL, MIPI_BGPD, MIPI_BGPD);
	inno_update_bits(inno, INNO_PHY_LVDS_CTRL, LVDS_BGPD, LVDS_BGPD);
}

static int inno_mipi_dphy_power_on(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct inno_mipi_dphy *inno = conn_state->phy_private;

	inno_mipi_dphy_bgpd_enable(inno);
	inno_mipi_dphy_da_pwrok_enable(inno);
	inno_mipi_dphy_pll_ldo_enable(inno);
	inno_mipi_dphy_lane_enable(inno);
	inno_mipi_dphy_reset(inno);
	inno_mipi_dphy_timing_init(inno);
	udelay(1);

	return 0;
}

static inline void inno_mipi_dphy_lane_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, INNO_PHY_LANE_CTRL, 0x7c, 0x00);
}

static int inno_mipi_dphy_power_off(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct inno_mipi_dphy *inno = conn_state->phy_private;

	inno_mipi_dphy_lane_disable(inno);
	inno_mipi_dphy_pll_ldo_disable(inno);
	inno_mipi_dphy_da_pwrok_disable(inno);
	inno_mipi_dphy_bgpd_disable(inno);

	return 0;
}

static unsigned long inno_mipi_dphy_set_pll(struct display_state *state,
					    unsigned long rate)
{
	struct connector_state *conn_state = &state->conn_state;
	struct inno_mipi_dphy *inno = conn_state->phy_private;
	unsigned long fin, fout;
	u16 fbdiv = 0;
	u8 prediv = 0;
	u32 m, v;

	fin = 24000000;
	fout = inno_mipi_dphy_pll_round_rate(fin, rate, &prediv, &fbdiv);

	debug("%s: fin=%lu, fout=%lu, prediv=%u, fbdiv=%u\n",
	       __func__, fin, fout, prediv, fbdiv);

	m = FBDIV_HI_MASK | PREDIV_MASK;
	v = FBDIV_HI(fbdiv >> 8) | PREDIV(prediv);
	inno_update_bits(inno, INNO_PHY_PLL_CTRL_0, m, v);

	m = FBDIV_LO_MASK;
	v = FBDIV_LO(fbdiv);
	inno_update_bits(inno, INNO_PHY_PLL_CTRL_1, m, v);

	inno->lane_mbps = fout / USEC_PER_SEC;

	return fout;
}

static int inno_mipi_dphy_parse_dt(ofnode panel_node, struct inno_mipi_dphy *inno)
{
	int format;

	inno->lanes = ofnode_read_s32_default(panel_node, "dsi,lanes", -1);
	if (inno->lanes < 0)
		inno->lanes = 4;

	format = ofnode_read_s32_default(panel_node, "dsi,format", -1);
	inno->bpp = mipi_dsi_pixel_format_to_bpp(format);
	if (inno->bpp < 0)
		inno->bpp = 24;

	return 0;
}

static int inno_mipi_dphy_init(struct display_state *state)
{
	const void *blob = state->blob;
	struct connector_state *conn_state = &state->conn_state;
	struct panel_state *panel_state = &state->panel_state;
	ofnode node = conn_state->phy_node;
	ofnode panel_node = panel_state->node;
	struct inno_mipi_dphy *inno;
	int ret;

	inno = malloc(sizeof(*inno));
	if (!inno)
		return -ENOMEM;

	inno->blob = blob;
	inno->node = node;

	ret = inno_mipi_dphy_parse_dt(panel_node, inno);
	if (ret) {
		printf("%s: failed to parse DT\n", __func__);
		return ret;
	}

	inno->regs = (void __iomem *)ofnode_get_addr(node);

	conn_state->phy_private = inno;

	return 0;
}

const struct rockchip_phy_funcs inno_mipi_dphy_funcs = {
	.init = inno_mipi_dphy_init,
	.power_on = inno_mipi_dphy_power_on,
	.power_off = inno_mipi_dphy_power_off,
	.set_pll = inno_mipi_dphy_set_pll,
};
