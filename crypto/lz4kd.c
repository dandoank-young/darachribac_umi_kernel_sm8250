// SPDX-License-Identifier: GPL-2.0-only
/*
 * LZ4KD - LZ4 Kernel Decompression
 *
 * Optimized LZ4 decompressor for kernel-space using LZ4_decompress_fast.
 * Faster than the standard LZ4_decompress_safe used by CRYPTO_LZ4.
 *
 * Based on CRYPTO_LZ4 implementation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/vmalloc.h>
#include <linux/lz4.h>
#include <crypto/internal/scomp.h>

struct lz4kd_ctx {
	void *lz4_comp_mem;
};

static void *lz4kd_alloc_ctx(struct crypto_scomp *tfm)
{
	void *ctx = vmalloc(LZ4_MEM_COMPRESS);
	if (!ctx)
		return ERR_PTR(-ENOMEM);
	return ctx;
}

static void lz4kd_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	vfree(ctx);
}

static int lz4kd_init(struct crypto_tfm *tfm)
{
	struct lz4kd_ctx *ctx = crypto_tfm_ctx(tfm);
	ctx->lz4_comp_mem = lz4kd_alloc_ctx(NULL);
	if (IS_ERR(ctx->lz4_comp_mem))
		return PTR_ERR(ctx->lz4_comp_mem);
	return 0;
}

static void lz4kd_exit(struct crypto_tfm *tfm)
{
	struct lz4kd_ctx *ctx = crypto_tfm_ctx(tfm);
	lz4kd_free_ctx(NULL, ctx->lz4_comp_mem);
}

static int __lz4kd_compress_crypto(const u8 *src, unsigned int slen,
				    u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = LZ4_compress_default(src, dst, slen, *dlen, ctx);
	if (!out_len)
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int lz4kd_scompress(struct crypto_scomp *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen,
			    void *ctx)
{
	return __lz4kd_compress_crypto(src, slen, dst, dlen, ctx);
}

static int lz4kd_compress_crypto(struct crypto_tfm *tfm, const u8 *src,
				 unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct lz4kd_ctx *ctx = crypto_tfm_ctx(tfm);
	return __lz4kd_compress_crypto(src, slen, dst, dlen, ctx->lz4_comp_mem);
}

static int __lz4kd_decompress_crypto(const u8 *src, unsigned int slen,
				     u8 *dst, unsigned int *dlen, void *ctx)
{
	/* Use LZ4_decompress_fast for faster decompression when output size is known */
	int out_len = LZ4_decompress_fast(src, dst, *dlen);
	if (out_len < 0)
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int lz4kd_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			     unsigned int slen, u8 *dst, unsigned int *dlen,
			     void *ctx)
{
	return __lz4kd_decompress_crypto(src, slen, dst, dlen, NULL);
}

static int lz4kd_decompress_crypto(struct crypto_tfm *tfm, const u8 *src,
				    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __lz4kd_decompress_crypto(src, slen, dst, dlen, NULL);
}

static struct crypto_alg alg_lz4kd = {
	.cra_name		= "lz4kd",
	.cra_driver_name	= "lz4kd-generic",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct lz4kd_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= lz4kd_init,
	.cra_exit		= lz4kd_exit,
	.cra_u			= {
		.compress = {
			.coa_compress		= lz4kd_compress_crypto,
			.coa_decompress		= lz4kd_decompress_crypto,
		}
	}
};

static struct scomp_alg scomp_lz4kd = {
	.alloc_ctx		= lz4kd_alloc_ctx,
	.free_ctx		= lz4kd_free_ctx,
	.compress		= lz4kd_scompress,
	.decompress		= lz4kd_sdecompress,
	.base			= {
		.cra_name	= "lz4kd",
		.cra_driver_name	= "lz4kd-scomp",
		.cra_priority		= 100,
		.cra_module		= THIS_MODULE,
	}
};

static int __init lz4kd_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg_lz4kd);
	if (ret)
		return ret;

	ret = crypto_register_scomp(&scomp_lz4kd);
	if (ret) {
		crypto_unregister_alg(&alg_lz4kd);
		return ret;
	}

	return 0;
}

static void __exit lz4kd_mod_fini(void)
{
	crypto_unregister_scomp(&scomp_lz4kd);
	crypto_unregister_alg(&alg_lz4kd);
}

module_init(lz4kd_mod_init);
module_exit(lz4kd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZ4KD Kernel Decompression (fast decompression)");
MODULE_ALIAS_CRYPTO("lz4kd");
MODULE_ALIAS_CRYPTO("lz4kd-generic");
MODULE_ALIAS_CRYPTO("lz4kd-scomp");
