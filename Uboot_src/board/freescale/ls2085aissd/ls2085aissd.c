/*
 * Copyright 2015 Freescale Semiconductor
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <malloc.h>
#include <errno.h>
#include <netdev.h>
#include <fsl_ifc.h>
#include <fsl_ddr.h>
#include <asm/io.h>
#include <fdt_support.h>
#include <libfdt.h>
#include <fsl_debug_server.h>
#include <fsl-mc/fsl_mc.h>
#include <environment.h>
#include <i2c.h>
#include <rtc.h>
#include <asm/arch-fsl-lsch3/soc.h>
#include <hwconfig.h>
#include <fsl_sec.h>

#ifndef CONFIG_ISSD
#include "../common/qixis.h"
#include "ls2080aqds_qixis.h"
#endif
#define SCFG_QSPICLKCTRL_DIV_20	(5 << 27)

#ifndef CONFIG_ISSD
#define PIN_MUX_SEL_SDHC	0x00
#define PIN_MUX_SEL_DSPI	0x0a

#define SET_SDHC_MUX_SEL(reg, value)	((reg & 0xf0) | value)
#endif

DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_ISSD
enum {
	MUX_TYPE_SDHC,
	MUX_TYPE_DSPI,
};

unsigned long long get_qixis_addr(void)
{
	unsigned long long addr;

	if (gd->flags & GD_FLG_RELOC)
		addr = QIXIS_BASE_PHYS;
	else
		addr = QIXIS_BASE_PHYS_EARLY;

	/*
	 * IFC address under 256MB is mapped to 0x30000000, any address above
	 * is mapped to 0x5_10000000 up to 4GB.
	 */
	addr = addr  > 0x10000000 ? addr + 0x500000000ULL : addr + 0x30000000;

	return addr;
}
#endif

int checkboard(void)
{
	char buf[64];
#ifndef CONFIG_ISSD
	u8 sw;
	static const char *const freq[] = {"100", "125", "156.25",
					    "100 separate SSCG"};
	int clock;
#endif

	cpu_name(buf);
#ifndef CONFIG_ISSD
	printf("Board: %s-QDS\n", buf);
#endif

#ifndef CONFIG_ISSD
	sw = QIXIS_READ(arch);
	printf("Board Arch: V%d, ", sw >> 4);
	printf("Board version: %c, boot from ", (sw & 0xf) + 'A' - 1);

	memset((u8 *)buf, 0x00, ARRAY_SIZE(buf));

	sw = QIXIS_READ(brdcfg[0]);
	sw = (sw & QIXIS_LBMAP_MASK) >> QIXIS_LBMAP_SHIFT;

	if (sw < 0x8)
		printf("vBank: %d\n", sw);
	else if (sw == 0x8)
		puts("PromJet\n");
	else if (sw == 0x9)
		puts("NAND\n");
	else if (sw == 0x15)
		printf("IFCCard\n");
	else
		printf("invalid setting of SW%u\n", QIXIS_LBMAP_SWITCH);

	printf("FPGA: v%d (%s), build %d",
	       (int)QIXIS_READ(scver), qixis_read_tag(buf),
	       (int)qixis_read_minor());
	/* the timestamp string contains "\n" at the end */
	printf(" on %s", qixis_read_time(buf));

	/*
	 * Display the actual SERDES reference clocks as configured by the
	 * dip switches on the board.  Note that the SWx registers could
	 * technically be set to force the reference clocks to match the
	 * values that the SERDES expects (or vice versa).  For now, however,
	 * we just display both values and hope the user notices when they
	 * don't match.
	 */
	puts("SERDES1 Reference : ");
	sw = QIXIS_READ(brdcfg[2]);
	clock = (sw >> 6) & 3;
	printf("Clock1 = %sMHz ", freq[clock]);
	clock = (sw >> 4) & 3;
	printf("Clock2 = %sMHz", freq[clock]);

	puts("\nSERDES2 Reference : ");
	clock = (sw >> 2) & 3;
	printf("Clock1 = %sMHz ", freq[clock]);
	clock = (sw >> 0) & 3;
	printf("Clock2 = %sMHz\n", freq[clock]);

#endif
	return 0;
}

#ifndef CONFIG_ISSD
unsigned long get_board_sys_clk(void)
{
	u8 sysclk_conf = QIXIS_READ(brdcfg[1]);

	switch (sysclk_conf & 0x0F) {
	case QIXIS_SYSCLK_83:
		return 83333333;
	case QIXIS_SYSCLK_100:
		return 100000000;
	case QIXIS_SYSCLK_125:
		return 125000000;
	case QIXIS_SYSCLK_133:
		return 133333333;
	case QIXIS_SYSCLK_150:
		return 150000000;
	case QIXIS_SYSCLK_160:
		return 160000000;
	case QIXIS_SYSCLK_166:
		return 166666666;
	}
	return 66666666;
}

unsigned long get_board_ddr_clk(void)
{
	u8 ddrclk_conf = QIXIS_READ(brdcfg[1]);

	switch ((ddrclk_conf & 0x30) >> 4) {
	case QIXIS_DDRCLK_100:
		return 100000000;
	case QIXIS_DDRCLK_125:
		return 125000000;
	case QIXIS_DDRCLK_133:
		return 133333333;
	}
	return 66666666;
}
#endif

int select_i2c_ch_pca9547(u8 ch)
{
	int ret;

	ret = i2c_write(I2C_MUX_PCA_ADDR_PRI, 0, 1, &ch, 1);
	if (ret) {
		puts("PCA: failed to select proper channel\n");
		return ret;
	}

	return 0;
}

#ifndef CONFIG_ISSD
int config_board_mux(int ctrl_type)
{
	u8 reg5;

	reg5 = QIXIS_READ(brdcfg[5]);

	switch (ctrl_type) {
	case MUX_TYPE_SDHC:
		reg5 = SET_SDHC_MUX_SEL(reg5, PIN_MUX_SEL_SDHC);
		break;
	case MUX_TYPE_DSPI:
		reg5 = SET_SDHC_MUX_SEL(reg5, PIN_MUX_SEL_DSPI);
		break;
	default:
		printf("Wrong mux interface type\n");
		return -1;
	}

	QIXIS_WRITE(brdcfg[5], reg5);

	return 0;
}
#endif

int board_init(void)
{
#ifndef CONFIG_ISSD
	char *env_hwconfig;
	u32 __iomem *dcfg_ccsr = (u32 __iomem *)DCFG_BASE;
	u32 val;
#endif

	init_final_memctl_regs();

#ifndef CONFIG_ISSD
	val = in_le32(dcfg_ccsr + DCFG_RCWSR13 / 4);

	env_hwconfig = getenv("hwconfig");

	if (hwconfig_f("dspi", env_hwconfig) &&
	    DCFG_RCWSR13_DSPI == (val & (u32)(0xf << 8)))
		config_board_mux(MUX_TYPE_DSPI);
	else
		config_board_mux(MUX_TYPE_SDHC);
#endif
	#if defined(CONFIG_NAND) && defined(CONFIG_FSL_QSPI)
		val = in_le32(dcfg_ccsr + DCFG_RCWSR15 / 4);

		if (DCFG_RCWSR15_IFCGRPABASE_QSPI == (val & (u32)0x3))
			QIXIS_WRITE(brdcfg[9],
				    (QIXIS_READ(brdcfg[9]) & 0xf8) |
				    FSL_QIXIS_BRDCFG9_QSPI);
	#endif

#ifdef CONFIG_ENV_IS_NOWHERE
	gd->env_addr = (ulong)&default_environment[0];
#endif
	select_i2c_ch_pca9547(I2C_MUX_CH_DEFAULT);
#ifndef CONFIG_ISSD
	rtc_enable_32khz_output();

#endif
	return 0;
}

int board_early_init_f(void)
{
	fsl_lsch3_early_init_f();
#ifdef CONFIG_FSL_QSPI
	/* input clk: 1/2 platform clk, output: input/20 */
	out_le32(SCFG_BASE + SCFG_QSPICLKCTLR, SCFG_QSPICLKCTRL_DIV_20);
#endif
	return 0;
}
#ifdef CONFIG_SYS_FSL_HAS_DP_DDR
void detail_board_ddr_info(void)
{
	puts("\nDDR    ");
	print_size(gd->bd->bi_dram[0].size + gd->bd->bi_dram[1].size, "");
	print_ddr_info(0);
	if (gd->bd->bi_dram[2].size) {
		puts("\nDP-DDR ");
		print_size(gd->bd->bi_dram[2].size, "");
		print_ddr_info(CONFIG_DP_DDR_CTRL);
	}
}
#endif
int dram_init(void)
{
	gd->ram_size = initdram(0);

	return 0;
}

#if defined(CONFIG_ARCH_MISC_INIT)
int arch_misc_init(void)
{
#ifdef CONFIG_FSL_DEBUG_SERVER
	debug_server_init();
#endif
#ifdef CONFIG_FSL_CAAM
	sec_init();
#endif
	return 0;
}
#endif

#ifdef CONFIG_FSL_MC_ENET
void fdt_fixup_board_enet(void *fdt)
{
	int offset;

	offset = fdt_path_offset(fdt, "/fsl-mc");

	if (offset < 0)
		offset = fdt_path_offset(fdt, "/fsl,dprc@0");

	if (offset < 0) {
		printf("%s: ERROR: fsl-mc node not found in device tree (error %d)\n",
		       __func__, offset);
		return;
	}

	if (get_mc_boot_status() == 0)
		fdt_status_okay(fdt, offset);
	else
		fdt_status_fail(fdt, offset);
}
#endif

#ifdef CONFIG_OF_BOARD_SETUP
int ft_board_setup(void *blob, bd_t *bd)
{
	int err = 0;
	u64 base[CONFIG_NR_DRAM_BANKS];
	u64 size[CONFIG_NR_DRAM_BANKS];

	ft_cpu_setup(blob, bd);

	/* fixup DT for the two GPP DDR banks */
	base[0] = gd->bd->bi_dram[0].start;
	size[0] = gd->bd->bi_dram[0].size;
	base[1] = gd->bd->bi_dram[1].start;
	size[1] = gd->bd->bi_dram[1].size;

	fdt_fixup_memory_banks(blob, base, size, 2);

#ifdef CONFIG_FSL_MC_ENET
	fdt_fixup_board_enet(blob);
	err = fsl_mc_ldpaa_exit(bd);
	if (err)
		return err;
#endif

	return 0;
}
#endif

#ifndef CONFIG_ISSD
void qixis_dump_switch(void)
{
	int i, nr_of_cfgsw;

	QIXIS_WRITE(cms[0], 0x00);
	nr_of_cfgsw = QIXIS_READ(cms[1]);

	puts("DIP switch settings dump:\n");
	for (i = 1; i <= nr_of_cfgsw; i++) {
		QIXIS_WRITE(cms[0], i);
		printf("SW%d = (0x%02x)\n", i, QIXIS_READ(cms[1]));
	}
}
#endif
