#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <linux/soc/sunxi/sun50i_h6_emce.h>

#define EMCE_KEY_REG(idx)		(0x00 + (idx) * 4)
#define EMCE_SALT_REG(idx)		(0x40 + (idx) * 4)

#define EMCE_MODE_REG			0x80
#define EMCE_MODE_SRC(val)			(((val) << 12) & GENMASK(12, 12))
#define EMCE_MODE_IV_KEY_LEN(val)		(((val) << 10) & GENMASK(11, 10))
#define EMCE_MODE_KEY_LEN(val)			(((val) << 4) & GENMASK(5, 4))
#define EMCE_MODE_MODE(val)			((val) & GENMASK(3, 0))

#define EMCE_KEY_MAX_SIZE	256
#define EMCE_SALT_MAX_SIZE	256

struct emce_priv {
	void __iomem	*base;

	struct clk	*bus_clk;
	struct clk	*mod_clk;

	struct reset_control *rst;
};

struct emce {
	struct emce_priv *priv;
	unsigned int	id;
};

static struct emce_priv *emce_global = NULL;

void sun50i_h6_emce_clear_key(struct emce *emce)
{
	struct emce_priv *priv = emce->priv;

	memset_io(priv->base + EMCE_KEY_REG(0),
		  0, EMCE_KEY_MAX_SIZE / 8);

	memset_io(priv->base + EMCE_SALT_REG(0),
		  0, EMCE_SALT_MAX_SIZE / 8);

	pr_crit("%s +%d 0x%x\n", __func__, __LINE__, readl(priv->base + 0x80));
}

int sun50i_h6_emce_program_key(struct emce *emce, const struct emce_key *key)
{
	struct emce_priv *priv = emce->priv;

	pr_crit("%s +%d 0x%x\n", __func__, __LINE__, readl(priv->base + 0x80));
	u32 mode = readl(priv->base + 0x80) & ~GENMASK(11, 0);

	pr_crit("%s +%d\n", __func__, __LINE__);
	switch (key->cipher) {
	case SUN50I_H6_EMCE_AES_ECB:
		mode |= EMCE_MODE_MODE(0);
		break;
	case SUN50I_H6_EMCE_AES_CBC:
		mode |= EMCE_MODE_MODE(1);
		break;
	case SUN50I_H6_EMCE_AES_XTS:
		mode |= EMCE_MODE_MODE(9);
		break;
	default:
		return -EINVAL;
	}

	pr_crit("%s +%d\n", __func__, __LINE__);
	switch (key->length) {
	case 128:
		mode |= EMCE_MODE_KEY_LEN(0);
		break;
	case 192:
		mode |= EMCE_MODE_KEY_LEN(1);
		break;
	case 256:
		mode |= EMCE_MODE_KEY_LEN(2);
		break;
	default:
		return -EINVAL;
	}

	pr_crit("%s +%d\n", __func__, __LINE__);
	switch (key->iv_length) {
	case 128:
		mode |= EMCE_MODE_IV_KEY_LEN(0);
		break;
	case 192:
		mode |= EMCE_MODE_IV_KEY_LEN(1);
		break;
	case 256:
		mode |= EMCE_MODE_IV_KEY_LEN(2);
		break;
	default:
		return -EINVAL;
	}

	pr_crit("%s +%d\n", __func__, __LINE__);
	sun50i_h6_emce_clear_key(emce);

	u32 *key_ptr = key->key;
	size_t idx = 0;
	size_t count = 0;
	while (count < 256) {
		pr_crit("%s +%d idx %d\n", __func__, __LINE__, idx);
		writel(*key_ptr++, priv->base + EMCE_KEY_REG(idx++));
		count += 32;
	}

	count = 0;
	idx = 0;
	while (count < 256) {
		writel(*key_ptr++, priv->base + EMCE_SALT_REG(idx++));
		count += 32;
	}

	writel(mode, priv->base + 0x80);
	pr_crit("%s +%d 0x%x\n", __func__, __LINE__, readl(priv->base + 0x80));

	return 0;
}

int sun50i_h6_emce_claim(struct emce *emce)
{
	struct emce_priv *priv = emce->priv;
	u32 reg;

	reg = readl(priv->base + 0x80) & ~BIT(12);
	writel(reg | EMCE_MODE_SRC(emce->id), priv->base + 0x80);

	pr_crit("%s +%d 0x%x\n", __func__, __LINE__, readl(priv->base + 0x80));

	return 0;
}
EXPORT_SYMBOL(sun50i_h6_emce_claim);

void sun50i_h6_emce_release(struct emce *emce)
{
}
EXPORT_SYMBOL(sun50i_h6_emce_release);

struct emce *devm_sun50i_h6_emce_get(struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct of_phandle_args args;
	struct emce *emce;
	int ret;

	if (!emce_global)
		return ERR_PTR(-EPROBE_DEFER);

	ret = of_parse_phandle_with_fixed_args(node, "allwinner,inline-crypto-controller", 1, 0,
					       &args);
	if (ret)
		return ERR_PTR(ret);

	emce = devm_kzalloc(dev, sizeof(*emce), GFP_KERNEL);
	if (!emce)
		return ERR_PTR(-ENOMEM);

	emce->priv = emce_global;
	emce->id = args.args[0];

	return emce;
}
EXPORT_SYMBOL(devm_sun50i_h6_emce_get);

static int sun50i_h6_emce_probe(struct platform_device *pdev)
{
	struct emce_priv *priv;
	struct resource *res;

	pr_crit("%s +%d\n", __func__, __LINE__);
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pr_crit("%s +%d\n", __func__, __LINE__);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	pr_crit("%s +%d\n", __func__, __LINE__);
	priv->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rst))
		return PTR_ERR(priv->rst);
	reset_control_deassert(priv->rst);

	pr_crit("%s +%d\n", __func__, __LINE__);
	priv->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(priv->bus_clk))
		return PTR_ERR(priv->bus_clk);
	clk_prepare_enable(priv->bus_clk);

	pr_crit("%s +%d\n", __func__, __LINE__);
	priv->mod_clk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(priv->mod_clk))
		return PTR_ERR(priv->mod_clk);
	clk_prepare_enable(priv->mod_clk);
	clk_set_rate(priv->mod_clk, 300000000);

	emce_global = priv;

	pr_crit("%s +%d 0x%x\n", __func__, __LINE__, readl(priv->base + 0x80));

	return 0;
}

static const struct of_device_id sun50i_h6_emce_dt_match[] = {
	{ .compatible = "allwinner,sun50i-h6-emce", },
	{ },
};
MODULE_DEVICE_TABLE(of, sun50i_h6_emce_dt_match);

static struct platform_driver sun50i_h6_emce_driver = {
	.driver = {
		.name		= "sun50i-h6-emce",
		.of_match_table	= sun50i_h6_emce_dt_match,
	},
	.probe	= sun50i_h6_emce_probe,
};
module_platform_driver(sun50i_h6_emce_driver);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_DESCRIPTION("Allwinner H6 Embedded Crypto Engine Driver");
MODULE_LICENSE("GPL");
