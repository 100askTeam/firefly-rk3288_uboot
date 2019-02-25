/*
 * (C) Copyright 2017 Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <asm/io.h>
#include <asm/gic.h>
#include <config.h>
#include <irq-generic.h>
#include "irq-gic.h"

#define gicd_readl(offset)	readl((void *)GICD_BASE + (offset))
#define gicc_readl(offset)	readl((void *)GICC_BASE + (offset))
#define gicd_writel(v, offset)	writel(v, (void *)GICD_BASE + (offset))
#define gicc_writel(v, offset)	writel(v, (void *)GICC_BASE + (offset))

#define IRQ_REG_X4(irq)		(4 * ((irq) / 4))
#define IRQ_REG_X16(irq)	(4 * ((irq) / 16))
#define IRQ_REG_X32(irq)	(4 * ((irq) / 32))
#define IRQ_REG_X4_OFFSET(irq)	((irq) % 4)
#define IRQ_REG_X16_OFFSET(irq)	((irq) % 16)
#define IRQ_REG_X32_OFFSET(irq)	((irq) % 32)

typedef enum INT_TRIG {
	INT_LEVEL_TRIGGER,
	INT_EDGE_TRIGGER
} eINT_TRIG;

struct gic_dist_data {
	uint32_t ctlr;
	uint32_t icfgr[DIV_ROUND_UP(1020, 16)];
	uint32_t itargetsr[DIV_ROUND_UP(1020, 4)];
	uint32_t ipriorityr[DIV_ROUND_UP(1020, 4)];
	uint32_t igroupr[DIV_ROUND_UP(1020, 32)];
	uint32_t ispendr[DIV_ROUND_UP(1020, 32)];
	uint32_t isenabler[DIV_ROUND_UP(1020, 32)];
};

struct gic_cpu_data {
	uint32_t ctlr;
	uint32_t pmr;
};

static struct gic_dist_data gicd_save;
static struct gic_cpu_data gicc_save;

__maybe_unused static u8 g_gic_cpumask = 0x01;

__maybe_unused static u32 gic_get_cpumask(void)
{
	u32 mask = 0, i;

	for (i = mask = 0; i < 32; i += 4) {
		mask = gicd_readl(GICD_ITARGETSRn + 4 * i);
		mask |= mask >> 16;
		mask |= mask >> 8;
		if (mask)
			break;
	}

	if (!mask)
		printf("GIC CPU mask not found.\n");

	debug("GIC CPU mask = 0x%08x\n", mask);
	return mask;
}

static inline void int_set_prio_filter(u32 priority)
{
	gicc_writel(priority & 0xff, GICC_PMR);
}

static inline void int_enable_distributor(void)
{
	u32 val;

	val = gicd_readl(GICD_CTLR);
	val |= 0x01;
	gicd_writel(val, GICD_CTLR);
}

static inline void int_disable_distributor(void)
{
	u32 val;

	val = gicd_readl(GICD_CTLR);
	val &= ~0x01;
	gicd_writel(val, GICD_CTLR);
}

static inline void int_enable_secure_signal(void)
{
	u32 val;

	val = gicc_readl(GICC_CTLR);
	val |= 0x01;
	gicc_writel(val, GICC_CTLR);
}

static inline void int_disable_secure_signal(void)
{
	u32 val;

	val = gicc_readl(GICC_CTLR);
	val &= ~0x01;
	gicc_writel(val, GICC_CTLR);
}

static inline void int_enable_nosecure_signal(void)
{
	u32 val;

	val = gicc_readl(GICC_CTLR);
	val |= 0x02;
	gicc_writel(val, GICC_CTLR);
}

static inline void int_disable_nosecure_signal(void)
{
	u32 val;

	val = gicc_readl(GICC_CTLR);
	val &= ~0x02;
	gicc_writel(val, GICC_CTLR);
}

static int gic_irq_set_trigger(int irq, eINT_TRIG trig)
{
	u32 val;

	if (trig == INT_LEVEL_TRIGGER) {
		val = gicd_readl(GICD_ICFGR + IRQ_REG_X16(irq));
		val &= ~(1 << (2 * IRQ_REG_X16_OFFSET(irq) + 1));
		gicd_writel(val, GICD_ICFGR + IRQ_REG_X16(irq));
	} else {
		val = gicd_readl(GICD_ICFGR + IRQ_REG_X16(irq));
		val |= (1 << (2 * IRQ_REG_X16_OFFSET(irq) + 1));
		gicd_writel(val, GICD_ICFGR + IRQ_REG_X16(irq));
	}

	return 0;
}

static int gic_irq_enable(int irq)
{
#ifdef CONFIG_GICV2
	u32 val;
	u32 shift = (irq % 4) * 8;

	if (irq >= PLATFORM_GIC_IRQS_NR)
		return -EINVAL;

	/* set enable */
	val = gicd_readl(GICD_ISENABLERn + IRQ_REG_X32(irq));
	val |= 1 << IRQ_REG_X32_OFFSET(irq);
	gicd_writel(val, GICD_ISENABLERn + IRQ_REG_X32(irq));


	/* set target */
	val = gicd_readl(GICD_ITARGETSRn + IRQ_REG_X4(irq));
	val &= ~(0xFF << shift);
	val |= (g_gic_cpumask << shift);
	gicd_writel(val, GICD_ITARGETSRn + IRQ_REG_X4(irq));

#else
	u32 val;

	val = gicd_readl(GICD_ISENABLERn + IRQ_REG_X32(irq));
	val |= 1 << IRQ_REG_X32_OFFSET(irq);
	gicd_writel(val, GICD_ISENABLERn + IRQ_REG_X32(irq));
#endif

	return 0;
}

static int gic_irq_disable(int irq)
{
	gicd_writel(1 << IRQ_REG_X32_OFFSET(irq),
		    GICD_ICENABLERn + IRQ_REG_X32(irq));

	return 0;
}

/*
 * irq_set_type - set the irq trigger type for an irq
 *
 * @irq: irq number
 * @type: IRQ_TYPE_{LEVEL,EDGE}_* value - see asm/arch/irq.h
 */
static int gic_irq_set_type(int irq, unsigned int type)
{
	unsigned int int_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
		int_type = 0x1;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_LEVEL_LOW:
		int_type = 0x0;
		break;
	default:
		return -EINVAL;
	}

	gic_irq_set_trigger(irq, int_type);

	return 0;
}

static void gic_irq_eoi(int irq)
{
#ifdef CONFIG_GICV2
	gicc_writel(irq, GICC_EOIR);
#else
	asm volatile("msr " __stringify(ICC_EOIR1_EL1) ", %0"
			: : "r" ((u64)irq));
	asm volatile("msr " __stringify(ICC_DIR_EL1) ", %0"
			: : "r" ((u64)irq));
	isb();
#endif
}

static int gic_irq_get(void)
{
#ifdef CONFIG_GICV2
	return gicc_readl(GICC_IAR) & 0x3fff; /* bit9 - bit0 */
#else
	u64 irqstat;

	asm volatile("mrs %0, " __stringify(ICC_IAR1_EL1) : "=r" (irqstat));
	return (u32)irqstat & 0x3ff;
#endif
}

static int gic_irq_suspend(void)
{
	int irq_nr, i, irq;

	/* irq nr */
	irq_nr = ((gicd_readl(GICD_TYPER) & 0x1f) + 1) * 32;
	if (irq_nr > 1020)
		irq_nr = 1020;

	/* GICC save */
	gicc_save.ctlr = gicc_readl(GICC_CTLR);
	gicc_save.pmr = gicc_readl(GICC_PMR);

	/* GICD save */
	gicd_save.ctlr = gicd_readl(GICD_CTLR);

	for (i = 0, irq = 0; irq < irq_nr; irq += 16)
		gicd_save.icfgr[i++] =
			gicd_readl(GICD_ICFGR + IRQ_REG_X16(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 4)
		gicd_save.itargetsr[i++] =
			gicd_readl(GICD_ITARGETSRn + IRQ_REG_X4(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 4)
		gicd_save.ipriorityr[i++] =
			gicd_readl(GICD_IPRIORITYRn + IRQ_REG_X4(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_save.igroupr[i++] =
			gicd_readl(GICD_IGROUPRn + IRQ_REG_X32(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_save.ispendr[i++] =
			gicd_readl(GICD_ISPENDRn + IRQ_REG_X32(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_save.isenabler[i++] =
			gicd_readl(GICD_ISENABLERn + IRQ_REG_X32(irq));

	dsb();

	return 0;
}

static int gic_irq_resume(void)
{
	int irq_nr, i, irq;

	irq_nr = ((gicd_readl(GICD_TYPER) & 0x1f) + 1) * 32;
	if (irq_nr > 1020)
		irq_nr = 1020;

	/* Disable ctrl register */
	gicc_writel(0, GICC_CTLR);
	gicd_writel(0, GICD_CTLR);
	dsb();

	/* Clear all interrupt */
	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_writel(0xffffffff,
			    GICD_ICENABLERn + IRQ_REG_X32(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 16)
		gicd_writel(gicd_save.icfgr[i++],
			    GICD_ICFGR + IRQ_REG_X16(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 4)
		gicd_writel(gicd_save.itargetsr[i++],
			    GICD_ITARGETSRn + IRQ_REG_X4(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 4)
		gicd_writel(gicd_save.ipriorityr[i++],
			    GICD_IPRIORITYRn + IRQ_REG_X4(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_writel(gicd_save.igroupr[i++],
			    GICD_IGROUPRn + IRQ_REG_X32(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_writel(gicd_save.isenabler[i++],
			    GICD_ISENABLERn + IRQ_REG_X32(irq));

	for (i = 0, irq = 0; irq < irq_nr; irq += 32)
		gicd_writel(gicd_save.ispendr[i++],
			    GICD_ISPENDRn + IRQ_REG_X32(irq));

	dsb();
	gicc_writel(gicc_save.pmr, GICC_PMR);
	gicc_writel(gicc_save.ctlr, GICC_CTLR);
	gicd_writel(gicd_save.ctlr, GICD_CTLR);
	dsb();

	return 0;
}

/**************************************regs save and resume**************************/
static int gic_irq_init(void)
{
	/* GICV3 done in: arch/arm/cpu/armv8/start.S */
#ifdef CONFIG_GICV2
	u32 val;

	/* end of interrupt */
	gicc_writel(PLATFORM_GIC_IRQS_NR, GICC_EOIR);

	/* disable gicc and gicd */
	gicc_writel(0, GICC_CTLR);
	gicd_writel(0, GICD_CTLR);

	/* disable interrupt */
	gicd_writel(0xffffffff, GICD_ICENABLERn + 0);
	gicd_writel(0xffffffff, GICD_ICENABLERn + 4);
	gicd_writel(0xffffffff, GICD_ICENABLERn + 8);
	gicd_writel(0xffffffff, GICD_ICENABLERn + 12);

	val = gicd_readl(GICD_ICFGR + 12);
	val &= ~(1 << 1);
	gicd_writel(val, GICD_ICFGR + 12);

	/* set interrupt priority threhold min: 256 */
	int_set_prio_filter(0xff);
	int_enable_secure_signal();
	int_enable_nosecure_signal();
	int_enable_distributor();

	g_gic_cpumask = gic_get_cpumask();
#endif

	return 0;
}

static struct irq_chip gic_irq_chip = {
	.name		= "gic-irq-chip",
	.irq_init	= gic_irq_init,
	.irq_suspend	= gic_irq_suspend,
	.irq_resume	= gic_irq_resume,
	.irq_get	= gic_irq_get,
	.irq_enable	= gic_irq_enable,
	.irq_disable	= gic_irq_disable,
	.irq_eoi	= gic_irq_eoi,
	.irq_set_type	= gic_irq_set_type,
};

struct irq_chip *arch_gic_irq_init(void)
{
	return &gic_irq_chip;
}
