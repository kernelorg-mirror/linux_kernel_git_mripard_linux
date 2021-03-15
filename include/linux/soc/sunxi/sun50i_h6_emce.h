/*
 * Allwinner H6 Embedded Crypto Controller Driver
 *
 * Copyright (C) 2021 Maxime Ripard
 *
 * Author: Maxime Ripard <maxime@cerno.tech>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __SUN50I_H6_EMCE_H_
#define __SUN50I_H6_EMCE_H_

#include <linux/types.h>

struct device;
struct emce;

enum emce_cipher {
	SUN50I_H6_EMCE_AES_ECB,
	SUN50I_H6_EMCE_AES_CBC,
	SUN50I_H6_EMCE_AES_XTS,
};

struct emce_key {
	enum emce_cipher cipher;
	size_t length;
	size_t iv_length;
	const void *key;
};

int sun50i_h6_emce_program_key(struct emce *emce, const struct emce_key *key);
void sun50i_h6_emce_clear_key(struct emce *emce);
int sun50i_h6_emce_claim(struct emce *emce);
void sun50i_h6_emce_release(struct emce *emce);
struct emce *devm_sun50i_h6_emce_get(struct device *dev);

#endif // __SUN50I_H6_EMCE_H_
