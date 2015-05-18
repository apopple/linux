#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/platform_device.h>

struct ast_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
};

static const struct pinctrl_pin_desc ast2400_pins_scu80[] = {
	PINCTRL_PIN(0,	"GPIOA0"),
	PINCTRL_PIN(1,	"GPIOA1"),
	PINCTRL_PIN(2,	"GPIOA2"),
	PINCTRL_PIN(3,	"GPIOA3"),

	PINCTRL_PIN(8,	"GPIOB0"),
	PINCTRL_PIN(9,	"GPIOB1"),
	PINCTRL_PIN(10,	"GPIOB2"),
	PINCTRL_PIN(11,	"GPIOB3"),
	PINCTRL_PIN(12,	"GPIOB4"),
	PINCTRL_PIN(13,	"GPIOB5"),
	PINCTRL_PIN(14,	"GPIOB6"),
	PINCTRL_PIN(15,	"GPIOB7"),

	PINCTRL_PIN(16,	"GPIOE0"),
	PINCTRL_PIN(17,	"GPIOE1"),
	PINCTRL_PIN(18,	"GPIOE2"),
	PINCTRL_PIN(19,	"GPIOE3"),
	PINCTRL_PIN(20,	"GPIOE4"),
	PINCTRL_PIN(21,	"GPIOE5"),
	PINCTRL_PIN(22,	"GPIOE6"),
	PINCTRL_PIN(23,	"GPIOE7"),

	PINCTRL_PIN(24,	"GPIOF0"),
	PINCTRL_PIN(25,	"GPIOF1"),
	PINCTRL_PIN(26,	"GPIOF2"),
	PINCTRL_PIN(27,	"GPIOF3"),
	PINCTRL_PIN(28,	"GPIOF4"),
	PINCTRL_PIN(29,	"GPIOF5"),
	PINCTRL_PIN(30,	"GPIOF6"),
	PINCTRL_PIN(31,	"GPIOF7"),
};

static const struct pinctrl_pin_desc ast2400_pins_scu90[] = {
	PINCTRL_PIN(0,	"GPIOA0"),
	PINCTRL_PIN(1,	"GPIOA1"),
	PINCTRL_PIN(2,	"GPIOA2"),
	PINCTRL_PIN(3,	"GPIOA3"),

	PINCTRL_PIN(8,	"GPIOB0"),
	PINCTRL_PIN(9,	"GPIOB1"),
	PINCTRL_PIN(10,	"GPIOB2"),
	PINCTRL_PIN(11,	"GPIOB3"),
	PINCTRL_PIN(12,	"GPIOB4"),
	PINCTRL_PIN(13,	"GPIOB5"),
	PINCTRL_PIN(14,	"GPIOB6"),
	PINCTRL_PIN(15,	"GPIOB7"),

	PINCTRL_PIN(16,	"GPIOE0"),
	PINCTRL_PIN(17,	"GPIOE1"),
	PINCTRL_PIN(18,	"GPIOE2"),
	PINCTRL_PIN(19,	"GPIOE3"),
	PINCTRL_PIN(20,	"GPIOE4"),
	PINCTRL_PIN(21,	"GPIOE5"),
	PINCTRL_PIN(22,	"GPIOE6"),
	PINCTRL_PIN(23,	"GPIOE7"),

	PINCTRL_PIN(24,	"GPIOF0"),
	PINCTRL_PIN(25,	"GPIOF1"),
	PINCTRL_PIN(26,	"GPIOF2"),
	PINCTRL_PIN(27,	"GPIOF3"),
	PINCTRL_PIN(28,	"GPIOF4"),
	PINCTRL_PIN(29,	"GPIOF5"),
	PINCTRL_PIN(30,	"GPIOF6"),
	PINCTRL_PIN(31,	"GPIOF7"),
};

static const struct pinctrl_pin_desc ast2400_pins_scuA0[] = {
	PINCTRL_PIN(0,	"RGMII1TXCK"),
	PINCTRL_PIN(1,	"RGMII1TXCTL"),
	PINCTRL_PIN(2,	"RGMII1TXD0"),
	PINCTRL_PIN(3,	"RGMII1TXD1"),
	PINCTRL_PIN(4,	"RGMII1TXD2"),
	PINCTRL_PIN(5,	"RGMII1TXD3"),

	PINCTRL_PIN(6,	"RGMII2TXCK"),
	PINCTRL_PIN(7,	"RGMII2TXCTL"),
	PINCTRL_PIN(8,	"RGMII2TXD0"),
	PINCTRL_PIN(9,	"RGMII2TXD1"),
	PINCTRL_PIN(10,	"RGMII2TXD2"),
	PINCTRL_PIN(11,	"RGMII2TXD3"),

	PINCTRL_PIN(12,	"RGMII1RXCK"),
	PINCTRL_PIN(13,	"RGMII1RXCTL"),
	PINCTRL_PIN(14,	"RGMII1RXD0"),
	PINCTRL_PIN(15,	"RGMII1RXD1"),
	PINCTRL_PIN(16,	"RGMII1RXD2"),
	PINCTRL_PIN(17,	"RGMII1RXD3"),

	PINCTRL_PIN(18,	"RGMII2RXCK"),
	PINCTRL_PIN(19,	"RGMII2RXCTL"),
	PINCTRL_PIN(20,	"RGMII2RXD0"),
	PINCTRL_PIN(21,	"RGMII2RXD1"),
	PINCTRL_PIN(22,	"RGMII2RXD2"),
	PINCTRL_PIN(23,	"RGMII2RXD3"),

	PINCTRL_PIN(24,	"ADC0"),
	PINCTRL_PIN(25,	"ADC1"),
	PINCTRL_PIN(26,	"ADC2"),
	PINCTRL_PIN(27,	"ADC3"),
	PINCTRL_PIN(28,	"ADC4"),
	PINCTRL_PIN(29,	"ADC5"),
	PINCTRL_PIN(30,	"ADC6"),
	PINCTRL_PIN(31,	"ADC7"),
};

static const struct pinctrl_pin_desc ast2400_pins_scuA4[] = {
	PINCTRL_PIN(0,	"ADC8"),
	PINCTRL_PIN(1,	"ADC9"),
	PINCTRL_PIN(2,	"ADC10"),
	PINCTRL_PIN(3,	"ADC11"),
	PINCTRL_PIN(4,	"ADC12"),
	PINCTRL_PIN(5,	"ADC13"),
	PINCTRL_PIN(6,	"ADC14"),
	PINCTRL_PIN(7,	"ADC15"),

	PINCTRL_PIN(7,	"GPIOY0"),
	PINCTRL_PIN(8,	"GPIOY1"),
	PINCTRL_PIN(9,	"GPIOY2"),
	PINCTRL_PIN(10,	"GPIOY3"),
};

static const unsigned int uart0_pins[] = {0, 3, 4};
static const unsigned int uart1_pins[] = {1, 2, 5};

static const struct ast_pin_group ast_pin_groups[] = {
	{
		.name = "uart0_grp",
		.pins = uart0_pins,
		.num_pins = ARRAY_SIZE(uart0_pins),
	},
	{
		.name = "uart1_grp",
		.pins = uart1_pins,
		.num_pins = ARRAY_SIZE(uart1_pins),
	},
};

static int ast_get_groups_count(struct pinctrl_dev *pctl)
{
	return ARRAY_SIZE(ast_pin_groups);
}

static const char *ast_get_group_name(struct pinctrl_dev *pctl,
					 unsigned selector)
{
	return ast_pin_groups[selector].name;
}

static int ast_get_group_pins(struct pinctrl_dev *pctl, unsigned selector,
				 const unsigned **pins, unsigned *num_pins)
{
	*pins = (unsigned *)ast_pin_groups[selector].pins;
	*num_pins = ast_pin_groups[selector].num_pins;
	return 0;
}

static struct pinctrl_ops ast_pinctrl_ops = {
	.get_groups_count = ast_get_groups_count,
	.get_group_name = ast_get_group_name,
	.get_group_pins = ast_get_group_pins,
};

static int ast_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned offset, unsigned long *config)
{
#if 0
	        struct my_conftype conf;

		//... Find setting for pin @ offset ...

		*config = (unsigned long) conf;
#endif

		return 0;
}

static int ast_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *configs, unsigned num_configs)
{
#if 0
	struct my_conftype *conf = (struct my_conftype *) config;

	switch (conf) {
	case PLATFORM_X_PULL_UP:
			break;
	}
#endif
	return 0;
}

static int ast_pinconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned selector,
				 unsigned long *config)
{
	return 0;
}

static int ast_pinconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned selector,
				 unsigned long *configs,
				 unsigned num_configs)
{
	return 0;
}

static struct pinconf_ops ast_pconf_ops = {
	.pin_config_get = ast_pinconf_get,
	.pin_config_set = ast_pinconf_set,
	.pin_config_group_get = ast_pinconf_group_get,
	.pin_config_group_set = ast_pinconf_group_set,
};


struct ast_pinctrl {
	struct device		*dev;
	struct pinctrl_dev	*pctl;
};

const struct pinctrl_pin_desc ast_pins[] = {
	PINCTRL_PIN(0, "0"),
	PINCTRL_PIN(1, "1"),
	PINCTRL_PIN(2, "2"),
	PINCTRL_PIN(3, "3"),
};

struct pinctrl_desc ast_desc = {
	.name = "Aspeed",
	.pins = ast_pins,
	.npins = ARRAY_SIZE(ast_pins),
	.pctlops = &ast_pinctrl_ops,
	.confops = &ast_pconf_ops,
	.owner = THIS_MODULE,
};


int ast_pinctrl_probe(struct platform_device *pdev)
{
	struct pinctrl_dev *pctl;

	pctl = pinctrl_register(&ast_desc, &pdev->dev, NULL);
	if (!pctl) {
		pr_err("could not register apseed pin driver\n");
		return -EIO;
	}

	return 0;
}
