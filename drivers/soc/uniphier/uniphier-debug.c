#include <linux/compiler.h>
#include <linux/init.h>
#include <asm/io.h>

#define UART_BASE	(0x54006a00)

#define UART_TX		0x00	/* Out: Transmit buffer */
#define UART_LCR_MCR	0x10	/* Line/Modem Control Register */
#define   UART_LCR_WLEN8	0x03	/* Wordlength: 8 bits */
#define UART_LSR	0x14	/* Line Status Register */
#define   UART_LSR_THRE		0x20	/* Transmit-hold-register empty */
#define UART_DLR	0x24	/* Divisor Latch Register */

static void __iomem *uart_base;

static void uart_putc(char c)
{
	while (!(readl(uart_base + UART_LSR) & UART_LSR_THRE))
		;

	writel(c, uart_base + UART_TX);
}

static void __putc(char c)
{
	uart_putc(c);
	if (c == '\n')
		uart_putc('\r');
}

static void __puts(const char *s)
{
	char c;

	while ((c = *s++))
		__putc(c);
}

#define get_num_va_args(args, lcount) \
	((lcount) > 1 ? va_arg(args, long long) :	\
	 ((lcount) ? va_arg(args, long) : va_arg(args, int)))

#define get_unum_va_args(args, lcount) \
	((lcount) > 1 ? va_arg(args, unsigned long long) :	\
	 ((lcount) ? va_arg(args, unsigned long) : va_arg(args, unsigned int)))

static void unsigned_num_print(unsigned long long unum, unsigned int radix)
{
	unsigned char buf[32];
	int i = 0, rem;

	do {
		rem = unum % radix;
		buf[i++] = rem > 9 ? rem - 0xa + 'a' : rem + '0';
	} while (unum /= radix);

	while (--i >= 0)
		__putc(buf[i]);
}

static void my_vprintk(const char *fmt, va_list args)
{
	int l_count;
	long long num;
	unsigned long long unum;
	char *str;
	char c;

	while ((c = *fmt++)) {
		l_count = 0;

		if (c != '%') {
			__putc(c);
			continue;
		}

		c = *fmt++;

		while (c == 'l') {
			l_count++;
			c = *fmt++;
		}

		if (c == 'z')
			l_count = 1;

		/* Check the format specifier */
		switch (c) {
		case '%':
			__putc('%');
			break;
		case 'i':
		case 'd':
			num = get_num_va_args(args, l_count);
			if (num < 0) {
				__putc('-');
				unum = -num;
			} else
				unum = num;

			unsigned_num_print(unum, 10);
			break;
		case 's':
			str = va_arg(args, char *);
			__puts(str);
			break;
		case 'p':
			unum = (unsigned long)va_arg(args, void *);
			if (unum)
				__puts("0x");

			unsigned_num_print(unum, 16);
			break;
		case 'x':
			unum = get_unum_va_args(args, l_count);
			unsigned_num_print(unum, 16);
			break;
		case 'u':
			unum = get_unum_va_args(args, l_count);
			unsigned_num_print(unum, 10);
			break;
		default:
			break;
		}
	}
}

void my_printk(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	my_vprintk(fmt, args);
	va_end(args);
}


static __init int uniphier_debug(void)
{
	uart_base = ioremap(UART_BASE, 0x40);

	if (!uart_base) {
		printk("UNIPHIER DEBUG failed to init!!!\n");
	}

	printk("UNIPHIER DEBUG success!\n");

	return 0;
}
early_initcall(uniphier_debug);
