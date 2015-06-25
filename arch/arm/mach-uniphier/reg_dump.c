
#if 0
/* debug */
static void uniphier_dump_show(phys_addr_t base, phys_addr_t end,
			       void __iomem *reg)
{
	int count = 0;

	for (; base < end; reg += 4, base += 4) {
		if (count == 0)
			printk("%08x: ", base);

		printk("%08x,", readl(reg));

		if (count == 3) {
			printk("\n");
			count = 0;
		} else {
			printk(" ");
			count++;
		}
	}
}

void uniphier_reg_dump(phys_addr_t base, size_t len)
{
	void __iomem *reg;

	reg = ioremap(base, len);
	if (!reg) {
		printk("could not ioremap for uniphier_reg_dump\n");
		return;
	}

	uniphier_dump_show(base, base + len, reg);

	iounmap(reg);
}

void uniphier_mem_dump(phys_addr_t base, size_t len)
{
	void __iomem *reg;

	reg = phys_to_virt(base);

	uniphier_dump_show(base, base + len, reg);
}

static inline unsigned long get_ttbr0(void)
{
	unsigned long val;
	asm("mrc p15, 0, %0, c2, c0, 0	@ get TTBR0" : "=r" (val) : : "cc");
	return val;
}

void uniphier_mmu_dump(phys_addr_t offset, size_t len)
{
	unsigned long mmu_base;

	mmu_base = get_ttbr0() & 0xffffc000;

	uniphier_mem_dump(mmu_base + offset, len);
}

void uniphier_mmu_entry(void __iomem *virt)
{
	unsigned long virtl = (unsigned long)virt;

	printk("entry for %lx is", virtl);

	uniphier_mmu_dump((virtl >> 20) * 4, 4);
}
#endif
