/*
 * Copyright 2015 Freescale Semiconductor
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <fsl_ifc.h>
#include <nand.h>
#include <spl.h>
#include <asm/arch-fsl-lsch3/config.h>
#include <asm/arch-fsl-lsch3/soc.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <ahci.h>
#include <scsi.h>
#include <fsl_validate.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_SYS_FSL_ERRATUM_A009635
#define PLATFORM_CYCLE_ENV_VAR	"a009635_interval_val"

static unsigned long get_internval_val_mhz(void)
{
	char *interval = getenv(PLATFORM_CYCLE_ENV_VAR);
	/*
	 *  interval is the number of platform cycles(MHz) between
	 *  wake up events generated by EPU.
	 */
	ulong interval_mhz = get_bus_freq(0) / (1000 * 1000);

	if (interval)
		interval_mhz = simple_strtoul(interval, NULL, 10);

	return interval_mhz;
}

void erratum_a009635(void)
{
	u32 val;
	unsigned long interval_mhz = get_internval_val_mhz();

	if (!interval_mhz)
		return;

	val = in_le32(DCSR_CGACRE5);
	writel(val | 0x00000200, DCSR_CGACRE5);

	val = in_le32(EPU_EPCMPR5);
	writel(interval_mhz, EPU_EPCMPR5);
	val = in_le32(EPU_EPCCR5);
	writel(val | 0x82820000, EPU_EPCCR5);
	val = in_le32(EPU_EPSMCR5);
	writel(val | 0x002f0000, EPU_EPSMCR5);
	val = in_le32(EPU_EPECR5);
	writel(val | 0x20000000, EPU_EPECR5);
	val = in_le32(EPU_EPGCR);
	writel(val | 0x80000000, EPU_EPGCR);
}
#endif

static void erratum_a008751(void)
{
#ifdef CONFIG_SYS_FSL_ERRATUM_A008751
	u32 __iomem *scfg = (u32 __iomem *)SCFG_BASE;

	writel(0x27672b2a, scfg + SCFG_USB3PRM1CR / 4);
#endif
}

static void erratum_rcw_src(void)
{
#if defined(CONFIG_SPL)
	u32 __iomem *dcfg_ccsr = (u32 __iomem *)DCFG_BASE;
	u32 __iomem *dcfg_dcsr = (u32 __iomem *)DCFG_DCSR_BASE;
	u32 val;

	val = in_le32(dcfg_ccsr + DCFG_PORSR1 / 4);
	val &= ~DCFG_PORSR1_RCW_SRC;
	val |= DCFG_PORSR1_RCW_SRC_NOR;
	out_le32(dcfg_dcsr + DCFG_DCSR_PORCR1 / 4, val);
#endif
}

#define I2C_DEBUG_REG 0x6
#define I2C_GLITCH_EN 0x8
/*
 * This erratum requires setting glitch_en bit to enable
 * digital glitch filter to improve clock stability.
 */
static void erratum_a009203(void)
{
	u8 __iomem *ptr;
#ifdef CONFIG_SYS_I2C
#ifdef I2C1_BASE_ADDR
	ptr = (u8 __iomem *)(I2C1_BASE_ADDR + I2C_DEBUG_REG);

	writeb(I2C_GLITCH_EN, ptr);
#endif
#ifdef I2C2_BASE_ADDR
	ptr = (u8 __iomem *)(I2C2_BASE_ADDR + I2C_DEBUG_REG);

	writeb(I2C_GLITCH_EN, ptr);
#endif
#ifdef I2C3_BASE_ADDR
	ptr = (u8 __iomem *)(I2C3_BASE_ADDR + I2C_DEBUG_REG);

	writeb(I2C_GLITCH_EN, ptr);
#endif
#ifdef I2C4_BASE_ADDR
	ptr = (u8 __iomem *)(I2C4_BASE_ADDR + I2C_DEBUG_REG);

	writeb(I2C_GLITCH_EN, ptr);
#endif
#endif
}
void bypass_smmu(void)
{
	u32 val;
	val = (in_le32(SMMU_SCR0) | SCR0_CLIENTPD_MASK) & ~(SCR0_USFCFG_MASK);
	out_le32(SMMU_SCR0, val);
	val = (in_le32(SMMU_NSCR0) | SCR0_CLIENTPD_MASK) & ~(SCR0_USFCFG_MASK);
	out_le32(SMMU_NSCR0, val);
}
void fsl_lsch3_early_init_f(void)
{
	erratum_a008751();
	erratum_rcw_src();
	init_early_memctl_regs();	/* tighten IFC timing */
	erratum_a009203();
#ifdef CONFIG_CHAIN_OF_TRUST
	/* In case of Secure Boot, the IBR configures the SMMU
	* to allow only Secure transactions.
	* SMMU must be reset in bypass mode.
	* Set the ClientPD bit and Clear the USFCFG Bit
	*/
	if (fsl_check_boot_mode_secure() == 1)
		bypass_smmu();
#endif
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{
	void __iomem *ahci_base;
	u32 *ppcfg, *ptc;

	/* FIXME: need to check is the serdes support SATA */
	/* settings for second controller */
	ahci_base = (void __iomem *)CONFIG_SYS_SATA2;
	ppcfg = (u32 *)(ahci_base + 0xa8);

	out_le32(ppcfg, 0xa003fffe);
	ptc = (u32 *)(ahci_base + 0xc8);
	out_le32(ptc, 0x08000025);

	/* settings for first controller */
	ahci_base = (void __iomem *)CONFIG_SYS_SATA1;
	ppcfg = (u32 *)(ahci_base + 0xa8);

	out_le32(ppcfg, 0xa003fffe);
	ptc = (u32 *)(ahci_base + 0xc8);
	out_le32(ptc, 0x08000025);

	ahci_init(ahci_base);
	scsi_scan(0);

#ifdef CONFIG_CHAIN_OF_TRUST
	fsl_setenv_chain_of_trust();
#endif

	return 0;
}
#endif

#ifdef CONFIG_SPL_BUILD
void board_init_f(ulong dummy)
{
	/* Clear global data */
	memset((void *)gd, 0, sizeof(gd_t));

	arch_cpu_init();
	board_early_init_f();
	timer_init();
	env_init();
	gd->baudrate = getenv_ulong("baudrate", 10, CONFIG_BAUDRATE);

	serial_init();
	console_init_f();
	dram_init();

	/* Clear the BSS. */
	memset(__bss_start, 0, __bss_end - __bss_start);

	board_init_r(NULL, 0);
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_NAND;
}
#endif
