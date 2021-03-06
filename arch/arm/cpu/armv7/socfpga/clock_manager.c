/*
 *  Copyright (C) 2012 Altera Corporation <www.altera.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/clock_manager.h>
#include <asm/arch/debug_memory.h>

#define CLKMGR_BYPASS_ENUM_ENABLE	1
#define CLKMGR_BYPASS_ENUM_DISABLE	0
#define CLKMGR_STAT_BUSY_ENUM_IDLE	0x0
#define CLKMGR_STAT_BUSY_ENUM_BUSY	0x1
#define CLKMGR_BYPASS_PERPLLSRC_ENUM_SELECT_EOSC1	0x0
#define CLKMGR_BYPASS_PERPLLSRC_ENUM_SELECT_INPUT_MUX	0x1
#define CLKMGR_BYPASS_SDRPLLSRC_ENUM_SELECT_EOSC1	0x0
#define CLKMGR_BYPASS_SDRPLLSRC_ENUM_SELECT_INPUT_MUX	0x1

#define CLEAR_BGP_EN_PWRDN \
	(CLKMGR_MAINPLLGRP_VCO_PWRDN_SET(0)| \
	CLKMGR_MAINPLLGRP_VCO_EN_SET(0)| \
	CLKMGR_MAINPLLGRP_VCO_BGPWRDN_SET(0))

#define VCO_EN_BASE \
	(CLKMGR_MAINPLLGRP_VCO_PWRDN_SET(0)| \
	CLKMGR_MAINPLLGRP_VCO_EN_SET(1)| \
	CLKMGR_MAINPLLGRP_VCO_BGPWRDN_SET(0))

static inline void cm_wait_for_lock(uint32_t mask)
{
	register uint32_t inter_val;
	do {
		inter_val = readl(SOCFPGA_CLKMGR_ADDRESS +
			CLKMGR_INTER_ADDRESS) & mask;
	} while (inter_val != mask);
}

/* function to poll in the fsm busy bit */
static inline void cm_wait4fsm(void)
{
	register uint32_t inter_val;
	do {
		inter_val = readl(SOCFPGA_CLKMGR_ADDRESS +
			CLKMGR_STAT_ADDRESS) & CLKMGR_STAT_BUSY_ENUM_BUSY;
	} while (inter_val);
}

/*
 * function to write the bypass register which requires a poll of the
 * busy bit
 */
static inline void cm_write_bypass(uint32_t val)
{
	writel(val, SOCFPGA_CLKMGR_ADDRESS + CLKMGR_BYPASS_ADDRESS);
	cm_wait4fsm();
}

/* function to write the ctrl register which requires a poll of the busy bit */
static inline void cm_write_ctrl(uint32_t val)
{
	writel(val, SOCFPGA_CLKMGR_ADDRESS + CLKMGR_CTRL_ADDRESS);
	cm_wait4fsm();
}

/* function to write a clock register that has phase information */
static inline void cm_write_with_phase(uint32_t value,
	uint32_t reg_address, uint32_t mask)
{
	/* poll until phase is zero */
	do {} while (readl(reg_address) & mask);

	writel(value, reg_address);

	do {} while (readl(reg_address) & mask);
}

/*
 * Setup clocks while making no assumptions of the
 * previous state of the clocks.
 *
 * Start by being paranoid and gate all sw managed clocks
 *
 * Put all plls in bypass
 *
 * Put all plls VCO registers back to reset value (bgpwr dwn).
 *
 * Put peripheral and main pll src to reset value to avoid glitch.
 *
 * Delay 5 us.
 *
 * Deassert bg pwr dn and set numerator and denominator
 *
 * Start 7 us timer.
 *
 * set internal dividers
 *
 * Wait for 7 us timer.
 *
 * Enable plls
 *
 * Set external dividers while plls are locking
 *
 * Wait for pll lock
 *
 * Assert/deassert outreset all.
 *
 * Take all pll's out of bypass
 *
 * Clear safe mode
 *
 * set source main and peripheral clocks
 *
 * Ungate clocks
 */

int cm_basic_init(const cm_config_t *cfg)
{
	uint32_t start, timeout;

	/* Start by being paranoid and gate all sw managed clocks */

	/*
	 * We need to disable nandclk
	 * and then do another apb access before disabling
	 * gatting off the rest of the periperal clocks.
	 */
	DEBUG_MEMORY
	writel(~CLKMGR_PERPLLGRP_EN_NANDCLK_MASK &
		readl(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_EN_ADDRESS),
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_EN_ADDRESS));

	/* DO NOT GATE OFF DEBUG CLOCKS & BRIDGE CLOCKS */
	writel(CLKMGR_MAINPLLGRP_EN_DBGTIMERCLK_MASK |
		CLKMGR_MAINPLLGRP_EN_DBGTRACECLK_MASK |
		CLKMGR_MAINPLLGRP_EN_DBGCLK_MASK |
		CLKMGR_MAINPLLGRP_EN_DBGATCLK_MASK |
		CLKMGR_MAINPLLGRP_EN_S2FUSER0CLK_MASK |
		CLKMGR_MAINPLLGRP_EN_L4MPCLK_MASK,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_EN_ADDRESS);

	writel(0, SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_EN_ADDRESS);

	/* now we can gate off the rest of the peripheral clocks */
	DEBUG_MEMORY
	writel(0, SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_EN_ADDRESS);

	/* Put all plls in bypass */
	DEBUG_MEMORY
	cm_write_bypass(
		CLKMGR_BYPASS_PERPLLSRC_SET(
		CLKMGR_BYPASS_PERPLLSRC_ENUM_SELECT_EOSC1) |
		CLKMGR_BYPASS_SDRPLLSRC_SET(
		CLKMGR_BYPASS_SDRPLLSRC_ENUM_SELECT_EOSC1) |
		CLKMGR_BYPASS_PERPLL_SET(CLKMGR_BYPASS_ENUM_ENABLE) |
		CLKMGR_BYPASS_SDRPLL_SET(CLKMGR_BYPASS_ENUM_ENABLE) |
		CLKMGR_BYPASS_MAINPLL_SET(CLKMGR_BYPASS_ENUM_ENABLE));

	/*
	 * Put all plls VCO registers back to reset value.
	 * Some code might have messed with them.
	 */
	DEBUG_MEMORY
	writel(CLKMGR_MAINPLLGRP_VCO_RESET_VALUE,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_VCO_ADDRESS);
	writel(CLKMGR_PERPLLGRP_VCO_RESET_VALUE,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_VCO_ADDRESS);
	writel(CLKMGR_SDRPLLGRP_VCO_RESET_VALUE,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_VCO_ADDRESS);

	/*
	 * The clocks to the flash devices and the L4_MAIN clocks can
	 * glitch when coming out of safe mode if their source values
	 * are different from their reset value.  So the trick it to
	 * put them back to their reset state, and change input
	 * after exiting safe mode but before ungating the clocks.
	 */
	writel(CLKMGR_PERPLLGRP_SRC_RESET_VALUE,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_SRC_ADDRESS);
	writel(CLKMGR_MAINPLLGRP_L4SRC_RESET_VALUE,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_L4SRC_ADDRESS);

	/* read back for the required 5 us delay. */
	readl(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_VCO_ADDRESS);
	readl(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_VCO_ADDRESS);
	readl(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_VCO_ADDRESS);


	/*
	 * We made sure bgpwr down was assert for 5 us. Now deassert BG PWR DN
	 * with numerator and denominator.
	 */
	DEBUG_MEMORY
	writel(cfg->main_vco_base | CLEAR_BGP_EN_PWRDN |
		CLKMGR_MAINPLLGRP_VCO_REGEXTSEL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_VCO_ADDRESS));

	writel(cfg->peri_vco_base | CLEAR_BGP_EN_PWRDN |
		CLKMGR_PERPLLGRP_VCO_REGEXTSEL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_VCO_ADDRESS));

	writel(CLKMGR_SDRPLLGRP_VCO_OUTRESET_SET(0) |
		CLKMGR_SDRPLLGRP_VCO_OUTRESETALL_SET(0) |
		cfg->sdram_vco_base | CLEAR_BGP_EN_PWRDN |
		CLKMGR_SDRPLLGRP_VCO_REGEXTSEL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_VCO_ADDRESS));

	/*
	 * Time starts here
	 * must wait 7 us from BGPWRDN_SET(0) to VCO_ENABLE_SET(1)
	 */
	reset_timer();
	start = get_timer(0);
	/* timeout in unit of us as CONFIG_SYS_HZ = 1000*1000 */
	timeout = 7;

	/* main mpu */
	DEBUG_MEMORY
	writel(cfg->mpuclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_MPUCLK_ADDRESS));

	/* main main clock */
	writel(cfg->mainclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_MAINCLK_ADDRESS));

	/* main for dbg */
	writel(cfg->dbgatclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_DBGATCLK_ADDRESS));

	/* main for cfgs2fuser0clk */
	writel(cfg->cfg2fuser0clk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_CFGS2FUSER0CLK_ADDRESS));

	/* Peri emac0 50 MHz default to RMII */
	writel(cfg->emac0clk,	(SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_EMAC0CLK_ADDRESS));

	/* Peri emac1 50 MHz default to RMII */
	writel(cfg->emac1clk,	(SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_EMAC1CLK_ADDRESS));

	/* Peri QSPI */
	writel(cfg->mainqspiclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_MAINQSPICLK_ADDRESS));

	writel(cfg->perqspiclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_PERQSPICLK_ADDRESS));

	/* Peri pernandsdmmcclk */
	writel(cfg->pernandsdmmcclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_PERNANDSDMMCCLK_ADDRESS));

	/* Peri perbaseclk */
	writel(cfg->perbaseclk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_PERBASECLK_ADDRESS));

	/* Peri s2fuser1clk */
	writel(cfg->s2fuser1clk, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_S2FUSER1CLK_ADDRESS));

	/* 7 us must have elapsed before we can enable the VCO */
	for ( ; get_timer(start) < timeout ; )
		;

	/* Enable vco */
	DEBUG_MEMORY
	/* main pll vco */
	writel(cfg->main_vco_base | VCO_EN_BASE,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_VCO_ADDRESS));

	/* periferal pll */
	writel(cfg->peri_vco_base | VCO_EN_BASE,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_VCO_ADDRESS));

	/* sdram pll vco */
	writel(CLKMGR_SDRPLLGRP_VCO_OUTRESET_SET(0) |
		CLKMGR_SDRPLLGRP_VCO_OUTRESETALL_SET(0) |
		cfg->sdram_vco_base | VCO_EN_BASE,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_VCO_ADDRESS));

	/* setup dividers while plls are locking */
	DEBUG_MEMORY

	/* L3 MP and L3 SP */
	writel(cfg->maindiv, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_MAINDIV_ADDRESS));

	writel(cfg->dbgdiv, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_DBGDIV_ADDRESS));

	writel(cfg->tracediv, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_TRACEDIV_ADDRESS));

	/* L4 MP, L4 SP, can0, and can1 */
	writel(cfg->perdiv, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_DIV_ADDRESS));

	writel(cfg->gpiodiv, (SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_GPIODIV_ADDRESS));

#define LOCKED_MASK \
	(CLKMGR_INTER_SDRPLLLOCKED_MASK  | \
	CLKMGR_INTER_PERPLLLOCKED_MASK  | \
	CLKMGR_INTER_MAINPLLLOCKED_MASK)

	DEBUG_MEMORY
	cm_wait_for_lock(LOCKED_MASK);

	/* write the sdram clock counters before toggling outreset all */
	DEBUG_MEMORY
	writel(cfg->ddrdqsclk & CLKMGR_SDRPLLGRP_DDRDQSCLK_CNT_MASK,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_DDRDQSCLK_ADDRESS);

	writel(cfg->ddr2xdqsclk & CLKMGR_SDRPLLGRP_DDR2XDQSCLK_CNT_MASK,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_DDR2XDQSCLK_ADDRESS);

	writel(cfg->ddrdqclk & CLKMGR_SDRPLLGRP_DDRDQCLK_CNT_MASK,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_DDRDQCLK_ADDRESS);

	writel(cfg->s2fuser2clk & CLKMGR_SDRPLLGRP_S2FUSER2CLK_CNT_MASK,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_S2FUSER2CLK_ADDRESS);

	/*
	 * after locking, but before taking out of bypass
	 * assert/deassert outresetall
	 */
	DEBUG_MEMORY
	uint32_t mainvco = readl(SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_VCO_ADDRESS);

	/* assert main outresetall */
	writel(mainvco | CLKMGR_MAINPLLGRP_VCO_OUTRESETALL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_VCO_ADDRESS));

	uint32_t periphvco = readl(SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_VCO_ADDRESS);

	/* assert pheriph outresetall */
	writel(periphvco | CLKMGR_PERPLLGRP_VCO_OUTRESETALL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_VCO_ADDRESS));

	/* assert sdram outresetall */
	writel(cfg->sdram_vco_base | VCO_EN_BASE|
		CLKMGR_SDRPLLGRP_VCO_OUTRESETALL_SET(1),
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_VCO_ADDRESS);

	/* deassert main outresetall */
	writel(mainvco & ~CLKMGR_MAINPLLGRP_VCO_OUTRESETALL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_VCO_ADDRESS));

	/* deassert pheriph outresetall */
	writel(periphvco & ~CLKMGR_PERPLLGRP_VCO_OUTRESETALL_MASK,
		(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_VCO_ADDRESS));

	/* deassert sdram outresetall */
	writel(CLKMGR_SDRPLLGRP_VCO_OUTRESETALL_SET(0) |
		       cfg->sdram_vco_base | VCO_EN_BASE,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_VCO_ADDRESS);

	/*
	 * now that we've toggled outreset all, all the clocks
	 * are aligned nicely; so we can change any phase.
	 */
	DEBUG_MEMORY
	cm_write_with_phase(cfg->ddrdqsclk,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_DDRDQSCLK_ADDRESS,
		CLKMGR_SDRPLLGRP_DDRDQSCLK_PHASE_MASK);

	/* SDRAM DDR2XDQSCLK */
	cm_write_with_phase(cfg->ddr2xdqsclk,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_DDR2XDQSCLK_ADDRESS,
		CLKMGR_SDRPLLGRP_DDR2XDQSCLK_PHASE_MASK);

	cm_write_with_phase(cfg->ddrdqclk,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_DDRDQCLK_ADDRESS,
		CLKMGR_SDRPLLGRP_DDRDQCLK_PHASE_MASK);

	cm_write_with_phase(cfg->s2fuser2clk,
		SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_S2FUSER2CLK_ADDRESS,
		CLKMGR_SDRPLLGRP_S2FUSER2CLK_PHASE_MASK);

	/* Take all three PLLs out of bypass when safe mode is cleared. */
	DEBUG_MEMORY
	cm_write_bypass(
		CLKMGR_BYPASS_PERPLLSRC_SET(
			CLKMGR_BYPASS_PERPLLSRC_ENUM_SELECT_EOSC1) |
		CLKMGR_BYPASS_SDRPLLSRC_SET(
			CLKMGR_BYPASS_SDRPLLSRC_ENUM_SELECT_EOSC1) |
		CLKMGR_BYPASS_PERPLL_SET(CLKMGR_BYPASS_ENUM_DISABLE) |
		CLKMGR_BYPASS_SDRPLL_SET(CLKMGR_BYPASS_ENUM_DISABLE) |
		CLKMGR_BYPASS_MAINPLL_SET(CLKMGR_BYPASS_ENUM_DISABLE));

	/* clear safe mode */
	DEBUG_MEMORY
	cm_write_ctrl(
		readl(SOCFPGA_CLKMGR_ADDRESS + CLKMGR_CTRL_ADDRESS) |
			CLKMGR_CTRL_SAFEMODE_SET(CLKMGR_CTRL_SAFEMODE_MASK));

	/*
	 * now that safe mode is clear with clocks gated
	 * it safe to change the source mux for the flashes the the L4_MAIN
	 */
	writel(cfg->persrc, SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_PERPLLGRP_SRC_ADDRESS);
	writel(cfg->l4src, SOCFPGA_CLKMGR_ADDRESS +
		CLKMGR_MAINPLLGRP_L4SRC_ADDRESS);

	/* Now ungate non-hw-managed clocks */
	DEBUG_MEMORY
	writel(~0, (SOCFPGA_CLKMGR_ADDRESS + CLKMGR_MAINPLLGRP_EN_ADDRESS));
	writel(~0, (SOCFPGA_CLKMGR_ADDRESS + CLKMGR_PERPLLGRP_EN_ADDRESS));
	writel(~0, (SOCFPGA_CLKMGR_ADDRESS + CLKMGR_SDRPLLGRP_EN_ADDRESS));

	return 0;
}
