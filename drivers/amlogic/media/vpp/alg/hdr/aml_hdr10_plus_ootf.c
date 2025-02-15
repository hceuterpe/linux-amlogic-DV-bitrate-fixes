// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 */

/* Standard Linux headers */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/amlogic/media/vout/vout_notify.h>
#include <linux/amlogic/media/vfm/vframe_provider.h>
#include <linux/amlogic/media/vfm/vframe_receiver.h>
#include "aml_hdr10_plus.h"
#include "aml_hdr10_plus_ootf.h"

unsigned int hdr10p_printk;
module_param(hdr10p_printk, uint, 0664);
MODULE_PARM_DESC(hdr10p_printk, "hdr10p_printk");

unsigned int force_refer_peak;
module_param(force_refer_peak, uint, 0664);
MODULE_PARM_DESC(force_refer_peak, "force_refer_peak");

#define pr_hdr(fmt, args...)\
	do {\
		if (hdr10p_printk)\
			pr_info(fmt, ## args);\
	} while (0)

static unsigned int _log2(unsigned int value)
{
	unsigned int ret;

	for (ret = 0; value > 1; ret++)
		value >>= 1;

	return ret;
}

/*EBZCurveParameters to gen OOTF bezier curves*/
static void ebzcurveparametersinit(struct ebzcurveparameters *ebzcurveparameters)
{
	int i;

	ebzcurveparameters->sx = 0;
	ebzcurveparameters->sy = 0;
	ebzcurveparameters->order = N;
	for (i = 0; i < N; i++)
		ebzcurveparameters->anchor[i] = PROCESSING_DATA_MAX;
}

/*percentile actually obtained from metadata */
static void percentileinit(struct percentiles *percentile,
	struct hdr10_plus_sei_s *hdr10_plus_sei)
{
	int i;

	percentile->num_percentile = hdr10_plus_sei->num_distributions[0];
	for (i = 0; i < percentile->num_percentile; i++) {
		percentile->percentilepercent[i] =
			hdr10_plus_sei->distribution_index[0][i];
		percentile->percentilevalue[i] =
			hdr10_plus_sei->distribution_values[0][i] / 10;
	}
}

/*metadata should obtained from 3C or 2094 metadata*/
static void metadatainit(struct scene2094metadata *metadata,
	struct hdr10_plus_sei_s *hdr10_plus_sei)
{
	int i;
	int found99percentage = 0;

	for (i = 0; i < hdr10_plus_sei->num_distributions[0]; i++) {
		if (hdr10_plus_sei->distribution_index[0][i] == 99)
			found99percentage = 1;
	}

	/*hdmitx vsif block have no maxscl,we can choose distribution value[8]*/
	/*if (!metadata->maxscenesourceluminance)*/
	/* from samsung sdk code, it used distribution first,*/
	/* if no 99 percentage, it will select maxscl*/
	if (found99percentage &&
		hdr10_plus_sei->distribution_values[0][8] != 0)
		metadata->maxscenesourceluminance =
			hdr10_plus_sei->distribution_values[0][8] / 10;
	else
		metadata->maxscenesourceluminance =
			max(max(hdr10_plus_sei->maxscl[0][0],
				hdr10_plus_sei->maxscl[0][1]),
				hdr10_plus_sei->maxscl[0][2]) / 10;

	metadata->minluminance = MIN_LUMINANCE;
	metadata->referenceluminance =
		hdr10_plus_sei->targeted_system_display_maximum_luminance;
	percentileinit(&metadata->percentiles, hdr10_plus_sei);

	metadata->ebzcurveparameters.sx = hdr10_plus_sei->knee_point_x[0];
	metadata->ebzcurveparameters.sy = hdr10_plus_sei->knee_point_y[0];
	metadata->ebzcurveparameters.order =
		hdr10_plus_sei->num_bezier_curve_anchors[0];

	for (i = 0; i < N - 1; i++)
		metadata->ebzcurveparameters.anchor[i] =
			hdr10_plus_sei->bezier_curve_anchors[0][i];
}

/* get EBZcurve params from metadata.*/
static void getmetadata(struct scene2094metadata *metadata,
	struct ebzcurveparameters *referencebezierparams)
{
	int i;

	if (metadata->ebzcurveparameters.order) {
		referencebezierparams->order =
			metadata->ebzcurveparameters.order + 1;
		referencebezierparams->sy = metadata->ebzcurveparameters.sy;
		referencebezierparams->sx = metadata->ebzcurveparameters.sx;

		for (i = 0; i < referencebezierparams->order; i++)
			referencebezierparams->anchor[i] =
				metadata->ebzcurveparameters.anchor[i];
	} else {
		referencebezierparams->order = 10;
	}
}

/*TV side default setting:  bezier curve params to gen basic OOTF curve*/
void hdr10p_basisootf_params_init(struct basisootf_params *basisootf_params)
{
	int i;
	int p2top9_max1_init[ORDER - 2] = {
		0.5582 * PROCESSING_DATA_MAX, 0.6745 * PROCESSING_DATA_MAX,
		0.7703 * PROCESSING_DATA_MAX, 0.8231 * PROCESSING_DATA_MAX,
		0.8729 * PROCESSING_DATA_MAX, 0.9130 * PROCESSING_DATA_MAX,
		0.9599 * PROCESSING_DATA_MAX, 0.9844 * PROCESSING_DATA_MAX
	};
	int p2top9_max2_init[ORDER - 2] = {
		0.4839 * PROCESSING_DATA_MAX, 0.6325 * PROCESSING_DATA_MAX,
		0.7253 * PROCESSING_DATA_MAX, 0.7722 * PROCESSING_DATA_MAX,
		0.8201 * PROCESSING_DATA_MAX, 0.8837 * PROCESSING_DATA_MAX,
		0.9208 * PROCESSING_DATA_MAX, 0.9580 * PROCESSING_DATA_MAX
	};

	/*u12*/
	basisootf_params->SY1_V1 = 0 << (PROCESSING_MAX - 12);
	basisootf_params->SY1_V2 = 1229 << (PROCESSING_MAX - 12);
	basisootf_params->SY1_T1 = 901 << (PROCESSING_MAX - 12);
	basisootf_params->SY1_T2 = 4095 << (PROCESSING_MAX - 12);
	basisootf_params->SY2_V1 = 0 << (PROCESSING_MAX - 12);
	basisootf_params->SY2_V2 = 819 << (PROCESSING_MAX - 12);
	basisootf_params->SY2_T1 = 1024 << (PROCESSING_MAX - 12);
	basisootf_params->SY2_T2 = 3890 << (PROCESSING_MAX - 12);

	/*KP mixing gain (final KP from bounds KP # 1 and KP # 2*/
	/*as a function of scene percentile) */
	basisootf_params->KP_G_V1 = 4095 << (PROCESSING_MAX - 12);
	basisootf_params->KP_G_V2 = 205 << (PROCESSING_MAX - 12);
	basisootf_params->KP_G_T1 = 205 << (PROCESSING_MAX - 12);
	basisootf_params->KP_G_T2 = 2048 << (PROCESSING_MAX - 12);

	/*Thresholds of minimum bound of P1 coefficient*/
	basisootf_params->P1_LIMIT_V1 = 3767 << (PROCESSING_MAX - 12);
	basisootf_params->P1_LIMIT_V2 = 4013 << (PROCESSING_MAX - 12);
	basisootf_params->P1_LIMIT_T1 = 41 << (PROCESSING_MAX - 12);
	basisootf_params->P1_LIMIT_T2 = 410 << (PROCESSING_MAX - 12);

	/*Thresholds to compute relative shape of curve (P2~P9 coefficient)*/
	/*by pre-defined bounds - as a function of scene percentile*/
	basisootf_params->P2TO9_T1 = 205 << (PROCESSING_MAX - 12);
	basisootf_params->P2TO9_T2 = 2252 << (PROCESSING_MAX - 12);

	for (i = 0; i < ORDER - 2; i++) {
		basisootf_params->P2TOP9_MAX1[i] = p2top9_max1_init[i];
		basisootf_params->P2TOP9_MAX2[i] = p2top9_max2_init[i];
	}

	/*Ps mixing gain (obtain all Ps coefficients) -*/
	/*as a function of TM dynamic compression ratio*/
	basisootf_params->PS_G_T1 = 512 << (PROCESSING_MAX - 12);
	basisootf_params->PS_G_T2 = 3890 << (PROCESSING_MAX - 12);

	/*Post-processing : Reduce P1/P2 (to enhance mid tone)*/
	/*for high TM dynamic range compression cases*/
	basisootf_params->LOW_SY_T1 = 20 << (PROCESSING_MAX - 12);
	basisootf_params->LOW_SY_T2 = 164 << (PROCESSING_MAX - 12);
	basisootf_params->LOW_K_T1 = 491 << (PROCESSING_MAX - 12);
	basisootf_params->LOW_K_T2 = 1638 << (PROCESSING_MAX - 12);
	basisootf_params->RED_P1_V1 = 2662 << (PROCESSING_MAX - 12);
	basisootf_params->RED_P1_T1 = 410 << (PROCESSING_MAX - 12);
	basisootf_params->RED_P1_T2 = 3071 << (PROCESSING_MAX - 12);
	basisootf_params->RED_P2_V1 = 3276 << (PROCESSING_MAX - 12);
	basisootf_params->RED_P2_T1 = 410 << (PROCESSING_MAX - 12);
	basisootf_params->RED_P2_T2 = 3071 << (PROCESSING_MAX - 12);
}

/*obtain percentile info on psl50 and psl99,*/
/*to calculate content average level and distribution later*/
static void getpercentile_50_99(struct percentiles *scenepercentiles,
	int *psll50, int *psll99)
{
	int i;
	int npsll = PERCENTILE_ORDER;
	int delta;

	/*Find exact percentiles if provided, else interpolate*/
	int psll50_1 = -1, per50_1 = -1;
	int psll50_2 = -1, per50_2 = -1;
	int psll99_1 = -1, per99_1 = -1;
	int psll99_2 = -1, per99_2 = -1;

	/*Search for exact percent index or bounds*/
	int curpercent = 0, prevpercent = 0;
	int curpsll = 0, prevpsll = 0;

	/*Set output to -1 (invalid)*/
	*psll50 = -1;
	*psll99 = -1;

	for (i = 0; i < npsll; i++) {
		if (i == 1 || i == 2)
			continue;

		curpercent = scenepercentiles->percentilepercent[i];
		curpsll = scenepercentiles->percentilevalue[i];
		if (curpercent == 50) {
			*psll50 = curpsll;
		} else if (*psll50 == -1 &&
			curpercent > 50 &&
			prevpercent < 50) {
			per50_1 = prevpercent;
			per50_2 = curpercent;
			psll50_1 = prevpsll;
			psll50_2 = curpsll;
		}

		if (curpercent == 99) {
			*psll99 = curpsll;
		} else if (*psll99 == -1 && curpercent > 99 &&
			prevpercent < 99) {
			per99_1 = prevpercent;
			per99_2 = curpercent;
			psll99_1 = prevpsll;
			psll99_2 = curpsll;
		}
		prevpercent = curpercent;
		prevpsll = curpsll;
	}

	if (*psll50 == -1) {
		delta = max(per50_2 - per50_1, 1);
		*psll50 = psll50_1 + (psll50_2 - psll50_1) *
			(50 - per50_1) / delta;
	}

	if (*psll99 == -1) {
		delta = max(per99_2 - per99_1, 1);
		*psll99 = psll99_1 + (psll99_2 - psll99_1) *
			(99 - per99_1) / delta;
	}
}

/* function to calculate linear interpolation*/
static int rampweight(int v1, int v2, int t1, int t2, int t)
{
	int retval = v1;

	if (t1 == t2) {
		retval = (t < t1) ? (v1) : (v2);
	} else {
		if (t <= t1)
			retval = v1;
		else if (t >= t2)
			retval = v2;
		else
			retval = v1 + (v2 - v1) * (t - t1) / (t2 - t1);
	}

	return retval;
}

/*p1 calculation based on default setting and*/
/*content statistic information(percentile)*/
int hdr10p_calcp1(int sx, int sy, int tgtl, int calcmaxl,
	struct basisootf_params *basisootf_params,
	int *p1_red_gain)
{
	int ax, ay, k, p1_limit, k2, p1_t, p1;
	int low_sy_g, high_k_g, high_tm_g, red_p1;

	ax = min(sx, PROCESSING_DATA_MAX);
	ay = min(sy, PROCESSING_DATA_MAX);

	k = tgtl * PROCESSING_DATA_MAX / max(tgtl, calcmaxl);
	p1_limit = rampweight(basisootf_params->P1_LIMIT_V2,
		basisootf_params->P1_LIMIT_V1,
		basisootf_params->P1_LIMIT_T1,
		basisootf_params->P1_LIMIT_T2,
		sy);
	k2 = (PROCESSING_DATA_MAX + 1 - ax) * PROCESSING_DATA_MAX /
		(ORDER * (PROCESSING_DATA_MAX + 1 - ay));
	p1_t = k2 * PROCESSING_DATA_MAX / k;
	p1 = max(min(p1_t, p1_limit), 0);
	low_sy_g = rampweight(PROCESSING_DATA_MAX + 1, 0,
		basisootf_params->LOW_SY_T1,
		basisootf_params->LOW_SY_T2,
		sy);
	high_k_g = rampweight(PROCESSING_DATA_MAX + 1, 0,
		basisootf_params->LOW_K_T1,
		basisootf_params->LOW_K_T2,
		k);
	high_tm_g = low_sy_g * high_k_g >> PROCESSING_MAX;
	red_p1 = rampweight(PROCESSING_DATA_MAX + 1,
		basisootf_params->RED_P1_V1,
		basisootf_params->RED_P1_T1,
		basisootf_params->RED_P1_T2,
		high_tm_g);
	p1 = min(max((p1 * red_p1) >> PROCESSING_MAX, P1MIN), p1_limit);

	if (p1_red_gain)
		*p1_red_gain = high_tm_g;

	return p1;
}

/*gen BasisOOTF parameters (sx,sy,p1~pn-1),based on content percentiles*/
	/*and default bezier params*/
static int basisootf(struct scene2094metadata *metadata,
	struct basisootf_params *basisootf_params, int productpeak,
	int sourcemaxl, struct ebzcurveparameters *scenebezierparams)
{
	int order = ORDER;
	int psll50, psll99, centerluminance, k, sy1, sy2, sy, sx, rem;
	int high_tm_g, p1, rem_p29, rem_ps, rem_red_p2, ps2to9[ORDER - 1];
	int coeffi, plin[ORDER - 1], pcoeff[ORDER - 1];
	int targetluminance = productpeak;
	int sourceluminance = max(targetluminance, sourcemaxl);
	int i;

	getpercentile_50_99(&metadata->percentiles, &psll50, &psll99);

	centerluminance = psll50 * PROCESSING_DATA_MAX / max(psll99, 1);
	k = targetluminance * PROCESSING_DATA_MAX / sourceluminance;

	if (hdr10p_printk & 2)
		pr_hdr("basisOOTF precision: ctrL=%d, k=%d\n",
			centerluminance, k);

	sy1 = rampweight(basisootf_params->SY1_V1,
		basisootf_params->SY1_V2,
		basisootf_params->SY1_T1,
		basisootf_params->SY1_T2,
		k);
	sy2 = rampweight(basisootf_params->SY2_V1,
		basisootf_params->SY2_V2,
		basisootf_params->SY2_T1,
		basisootf_params->SY2_T2,
		k);
	rem = rampweight(basisootf_params->KP_G_V1,
		basisootf_params->KP_G_V2,
		basisootf_params->KP_G_T1,
		basisootf_params->KP_G_T2,
		centerluminance);
	sy = (rem * sy1 + (PROCESSING_DATA_MAX + 1 - rem) * sy2) >>
		PROCESSING_MAX;
	sx = sy * k;

	/*P coefficient*/
	high_tm_g = 0;

	p1 = hdr10p_calcp1(sx, sy, targetluminance, sourcemaxl,
		basisootf_params, &high_tm_g);

	if (hdr10p_printk & 2)
		pr_hdr("basisOOTF precision: p1=(int) %d\n\n", p1);

	for (i = 0; i < NPCOEFF - 1; i++) {
		rem_p29 = rampweight(basisootf_params->P2TOP9_MAX2[i],
			basisootf_params->P2TOP9_MAX1[i],
			basisootf_params->P2TO9_T1,
			basisootf_params->P2TO9_T2,
			centerluminance);
		ps2to9[i] = (rem_p29 * basisootf_params->P2TOP9_MAX1[i] +
		(PROCESSING_DATA_MAX + 1 - rem_p29) *
		basisootf_params->P2TOP9_MAX2[i]) >> PROCESSING_MAX;
		if (hdr10p_printk & 2)
			pr_hdr("basisOOTF precision: g=%d ps2to9[%d] = %d\n",
				rem_p29, i, ps2to9[i]);
	}

	rem_ps = rampweight(PROCESSING_DATA_MAX + 1, 0,
		basisootf_params->PS_G_T1,
		basisootf_params->PS_G_T2, k);

	pcoeff[0] = p1;
	for (i = 1; i < NPCOEFF; i++) {
		coeffi = i + 1;
		plin[i] = (coeffi * PROCESSING_DATA_MAX / order);
		pcoeff[i] = max(min((rem_ps * ps2to9[i - 1] +
			(PROCESSING_DATA_MAX + 1 - rem_ps) * plin[i])
			>> PROCESSING_MAX,
			coeffi * p1),
			plin[i]);

		if (hdr10p_printk & 2)
			pr_hdr("basisOOTF precision: g=%d, pcoeff[%d] = %d\n",
				rem_ps, i, pcoeff[i]);
	}

	/*p[1] recalc precision:0.01,bad*/
	rem_red_p2 = rampweight(PROCESSING_DATA_MAX + 1,
		basisootf_params->RED_P2_V1,
		basisootf_params->RED_P2_T1,
		basisootf_params->RED_P2_T2,
		high_tm_g);
	pcoeff[1] = max(min((pcoeff[1] * rem_red_p2) >> PROCESSING_MAX, 2 * p1),
		2 * PROCESSING_DATA_MAX / ORDER);

	/*finally  pcoeff[ORDER-1],sy,sx*/
	scenebezierparams->sx = sx;
	scenebezierparams->sy = sy;

	/*only calc  ORDER(10)  < actual order(15)*/
	for (i = 0; i < NPCOEFF; i++)
		scenebezierparams->anchor[i] = pcoeff[i];

	if (hdr10p_printk & 2) {
		pr_hdr("default P setting:\n");
		for (i = 0; i < NPCOEFF; i++)
			pr_hdr("%2d: %d\n", i, pcoeff[i]);
	}

	return 0;
}

/* receive bezier parameter(Kx,ky,P)and return guided parameter base on*/
/*product panel luminance out: product curve params(Kx,ky,P) */
static int guidedootf(struct scene2094metadata *metadata,
	struct basisootf_params *basisootf_params,
	struct ebzcurveparameters *referencebezierparams,
	int productpeak,
	struct ebzcurveparameters *productbezierparams)
{
	int kp_bypass = 1229;
	int refenceluminance = metadata->referenceluminance;
	int productluminance = productpeak;
	int minluminance = metadata->minluminance;
	int maxluminance = metadata->maxscenesourceluminance;
	int order = referencebezierparams->order;
	int nump = order - 1;

	int blendcoeff, norm;
	int anchorlinear[N];
	int i;
	int ps1;

	struct ebzcurveparameters minbezierparams;

	ebzcurveparametersinit(&minbezierparams);

	for (i = 0; i < nump; i++)
		anchorlinear[i] = (i + 1) * PROCESSING_DATA_MAX / order;

	if (hdr10p_printk & 2) {
		pr_hdr("order = %d, linear ps[i]:\n",
			order);
		for (i = 0; i < nump; i++)
			pr_hdr(" %2d: %4d\n",
				i, anchorlinear[i]);
	}

	/*patch for low luminance panel tm calculate, find rootcause next step*/
	if (force_refer_peak) {
		refenceluminance = force_refer_peak;
		if (refenceluminance < 250)
			refenceluminance = 0;
	}

	/*---------case 0: productPeak < minL -----------------*/
	if (productluminance < minluminance) {
		if (hdr10p_printk & 2) {
			pr_hdr("case: P < minL\n\n...processing...\n");
			pr_hdr("error GuidedOOTF: < %d nit not supported\n",
				productluminance);
		}

		productbezierparams->sx = 0;
		productbezierparams->sy = 0;
		for (i = 0; i < nump; i++)
			productbezierparams->anchor[i] = anchorlinear[i];

		productbezierparams->order = order;
		return -1;
	}

	/*---------case 1: productPeak = ref -----------------*/
	if (productluminance == refenceluminance) {
		if (hdr10p_printk & 2)
			pr_hdr("case: P = ref\n\n...processing...\n");

		productbezierparams->sx = referencebezierparams->sx;
		productbezierparams->sy = referencebezierparams->sy;
		productbezierparams->order = referencebezierparams->order;

		for (i = 0; i < nump; i++)
			productbezierparams->anchor[i] =
				referencebezierparams->anchor[i];

		if (hdr10p_printk & 2) {
			pr_hdr("order:%2d,   (sx,sy):(%4d, %4d)\n",
				productbezierparams->order,
				productbezierparams->sx,
				productbezierparams->sy);
			for (i = 0; i < nump; i++)
				pr_hdr(" %2d: %4d\n",
					i, productbezierparams->anchor[i]);
		}
	} else if (productluminance > maxluminance) {
		/*---------case 2: productPeak > maxL -----------------*/
		if (hdr10p_printk & 2)
			pr_hdr("case: P > maxL\n\n...processing...\n");

		productbezierparams->sx = kp_bypass;
		productbezierparams->sy = kp_bypass;
		productbezierparams->order = order;

		for (i = 0; i < nump; i++)
			productbezierparams->anchor[i] = anchorlinear[i];
	} else {
		/*---------case 3: minL < productPeak < maxL -----------------*/
		productbezierparams->order = referencebezierparams->order;
		/*---------case 3.1: minL < productPeak < ref ----------------*/
		if (productluminance < refenceluminance) {
			if (hdr10p_printk & 2)
				pr_hdr("case: P < ref\n\n...processing...\n");

			norm = refenceluminance - minluminance;
			blendcoeff = refenceluminance - productluminance;

			basisootf(metadata, basisootf_params, minluminance,
				maxluminance, &minbezierparams);

			productbezierparams->sy =
				(blendcoeff * minbezierparams.sy +
				(norm - blendcoeff) * referencebezierparams->sy +
				(norm >> 1)) / norm;
			productbezierparams->sx =
			productbezierparams->sy * productluminance /
				maxluminance;

			for (i = 0; i < nump ; i++) {
				productbezierparams->anchor[i] =
					(blendcoeff * minbezierparams.anchor[i] +
					(norm - blendcoeff) *
				referencebezierparams->anchor[i] +
					(norm >> 1)) / norm;
			}

			if (hdr10p_printk & 2) {
				pr_hdr("p-1=>Anchor[i] = ");
				pr_hdr("(blendcoeff *referenceBezierParams->Anchor[i] ");
				pr_hdr("+ (norm - blendcoeff)*minBezierParams.Anchor[i]");
				pr_hdr(" + (norm>>1) )/norm\n");
				for (i = 0; i < nump; i++) {
					pr_hdr("p-1=> %2d: %4d = ",
						i,
						productbezierparams->anchor[i]);
					pr_hdr(" %4d * %4d + ",
						blendcoeff,
						referencebezierparams->anchor[i]);
					pr_hdr("(%4d - %4d) * %4d / %4d\n",
						norm,
						blendcoeff,
						minbezierparams.anchor[i],
						norm);
				}
			}
		} else {
			/*---------case 3.2: ref < productPeak < maxL ----------------*/
			if (hdr10p_printk & 2)
				pr_hdr("case: P > ref\n\n...processing...\n");

			norm = maxluminance - refenceluminance;
			blendcoeff = productluminance - refenceluminance;

			productbezierparams->sy = (blendcoeff * kp_bypass +
				(norm - blendcoeff) * referencebezierparams->sy +
				(norm >> 1)) / norm;
			productbezierparams->sx = productbezierparams->sy *
				productluminance / maxluminance;

			for (i = 0; i < nump; i++) {
				productbezierparams->anchor[i] =
					(blendcoeff * anchorlinear[i] +
					(norm - blendcoeff) *
				referencebezierparams->anchor[i] +
					(norm >> 1)) / norm;
			}

			if (hdr10p_printk & 2) {
				for (i = 0; i < nump; i++) {
					pr_hdr("p-1=> %2d: %4d = ",
						i,
						productbezierparams->anchor[i]);
					pr_hdr(" %4d * %4d + ",
						blendcoeff,
						anchorlinear[i]);
					pr_hdr("(%4d - %4d) * %4d / %4d\n",
						norm,
						blendcoeff,
						referencebezierparams->anchor[i],
						norm);
				}
			}
		}

		ps1 = hdr10p_calcp1(productbezierparams->sx,
			productbezierparams->sy,
			productluminance, maxluminance,
			basisootf_params, NULL);

		productbezierparams->anchor[0] = ps1;

		for (i = 1; i < nump; i++) {
			productbezierparams->anchor[i] =
				min(productbezierparams->anchor[i],
				(i + 1) * productbezierparams->anchor[0]);
		}

		if (hdr10p_printk & 2) {
			pr_hdr("p-2: %2d, %4d\n", 0, ps1);
			/*p process, form curve below CI line slope*/
			for (i = 1; i < nump; i++)
				pr_hdr("p-2=> %2d-> %d\n",
					i, productbezierparams->anchor[i]);
		}
	}

	return 0;
}

/*bezier optimism method to gen bezier function*/
static int decasteliau(u64 *beziercurve, u64 *anchor,
	u64 u, int order, u64 range_ebz_x)
{
	u64 pointy[N + 2];
	int i, j;

	for (i = 0; i < order + 1; i++)
		pointy[i] = anchor[i];

	for (i = 1; i <= order; i++)
		for (j = 0; j <= order - i; j++) {
			pointy[j] = (pointy[j] * (range_ebz_x - u) +
			pointy[j + 1] * u + range_ebz_x / 2);
			pointy[j] = div64_u64(pointy[j], range_ebz_x);
		}

	beziercurve[1] = pointy[0];
	return 0;
}

#define org_anchor

static u64 oo_lut_x[OOLUT_NUM] = {
	1, 16, 32, 64, 128, 256, 512, 1024, 2048, 2560, 3072, 3584, 4096,
	5120, 6144, 7168, 8192, 10240, 12288, 14336, 16384, 20480, 24576,
	28672, 32768, 40960, 49152, 57344, 65536, 81920, 98304, 114688,
	131072, 163840, 196608, 229376, 262144, 327680, 393216, 458752,
	524288, 655360, 786432, 917504, 1048576, 1179648, 1310720, 1441792,
	1572864, 1703936, 1835008, 1966080, 2097152, 2359296, 2621440, 2883584,
	3145728, 3407872, 3670016, 3932160, 4194304, 4718592, 5242880, 5767168,
	6291456, 6815744, 7340032, 7864320, 8388608, 9437184, 10485760,
	11534336, 12582912, 13631488, 14680064, 15728640, 16777216, 18874368,
	20971520, 23068672, 25165824, 27262976, 29360128, 31457280, 33554432,
	37748736, 41943040, 46137344, 50331648, 54525952, 58720256, 62914560,
	67108864, 75497472, 83886080, 92274688, 100663296, 109051904, 117440512,
	125829120, 134217728, 150994944, 167772160, 184549376, 201326592,
	218103808, 234881024, 251658240, 268435456, 301989888, 335544320,
	369098752, 402653184, 436207616, 469762048, 503316480, 536870912,
	603979776, 671088640, 738197504, 805306368, 872415232, 939524096,
	1006632960, 1073741824, 1207959552, 1342177280, 1476395008, 1610612736,
	1744830464, 1879048192, 2013265920ULL, 2147483648ULL, 2281701376ULL,
	2415919104ULL, 2550136832ULL, 2684354560ULL, 2818572288ULL,
	2952790016ULL, 3087007744ULL, 3221225472ULL, 3355443200ULL,
	3489660928ULL, 3623878656ULL, 3758096384ULL,
	3892314112ULL, 4026531840ULL, 4160749568ULL, 4294967296ULL
};

/*gen OOTF curve and gain from (sx,sy) and P1~pn-1*/
static int gen_ebzurve(u64 *curvex, u64 *curvey,
	unsigned int *gain,
	u64 nkx, uint64_t nky,
	u64 *anchor, int order)
{
	u64 my_anchor_y[N + 2];
	u64 temp;
	u64 kx, ky;

	u64 range_ebz_x;
	u64 range_ebz_y;

	u64 step_alpha;
	u64 beziercurve[2];
	int i;
	int nump;

	if (order > 1 && order <= N + 1)
		nump = order - 1;
	else
		nump = N - 1;

	/*u12->U16*/
	kx = nkx << (U32 - PROCESSING_MAX);
	ky = nky << (U32 - PROCESSING_MAX);

	range_ebz_x = _U32_MAX - kx;
	range_ebz_y = _U32_MAX - ky;

#ifdef org_anchor
	for (i = 0; i < nump; i++)/* u12-> ebz_y, u32*/
		/*anchorY default range:PROCESSING_MAX */
		my_anchor_y[i + 1] = anchor[i] << (U32 - PROCESSING_MAX);
	my_anchor_y[0] = 0;
	my_anchor_y[nump + 1] = _U32_MAX; /* u12 */
#else
	for (i = 0; i < nump; i++)/* u12-> ebz_y, u32*/
		/*anchorY default range:PROCESSING_MAX */
		my_anchor_y[i + 1] =
			(anchor[i] * range_ebz_y) >> PROCESSING_MAX;
	my_anchor_y[0] = 0;
	my_anchor_y[N] = range_ebz_y; /*u12 -> U32*/
#endif  /* org_anchor */

	for (i = 0; i < POINTS; i++)
		curvey[i] = 0;

	for (i = 0; i < POINTS; i++) {
		if (oo_lut_x[i] < kx) {
			curvex[i] = oo_lut_x[i];/*u32*/
			curvey[i] = oo_lut_x[i] * ky;
			curvey[i] = div64_u64(curvey[i], kx ? kx : 1);
			temp = curvey[i] << GAIN_BIT;/*u12*/
			gain[i] = div64_u64(temp, oo_lut_x[i]);
		} else {
			/*norm in Decasteliau() function*/
			step_alpha = (oo_lut_x[i] - kx);
			/* calc each point from 1st to N-th layer*/
			decasteliau(&beziercurve[0], my_anchor_y, step_alpha,
				order, range_ebz_x);
			#ifdef org_anchor /*range_ebz_y = _U32_MAX*/
			curvey[i] = ky +
				((range_ebz_y * beziercurve[1] + range_ebz_y / 2)
				>> U32);
			#else
			curvey[i] = ky + ((range_ebz_y * beziercurve[1] +
				range_ebz_y / 2));
			curvey[i] = div64_u64(curvey[i], range_ebz_y);
			#endif /*org_anchor*/

			/*CurveX[i] = Kx +*/
			/*((range_ebz_x * BezierCurve[0] + range_ebz_x / 2) / */
			/*range_ebz_x);*/
			curvex[i] = kx + step_alpha;
			temp = curvey[i] << GAIN_BIT;
			gain[i] = div64_u64(temp, oo_lut_x[i]);
			if (gain[i] < (1 << GAIN_BIT))
				gain[i] = 1 << GAIN_BIT;
		}
	}

	gain[POINTS - 1] = 1 << GAIN_BIT;
	return 0;
}

static void vframe_hdr_plus_sei_s_init(struct hdr10_plus_sei_s *hdr10_plus_sei)
{
	struct hdr10_plus_sei_s *p;
	struct vframe_hdr10p_sei *pvf;
	int i;
	int p_init[N - 1] = {181, 406, 607, 796, 834, 863, 890, 917, 938};
	int percentilepercent_init[PERCENTILE_ORDER] = {
		1, 5, 10, 25, 50, 75, 90, 95, 99};
	/*int percentilevalue_init[PERCENTILE_ORDER] = {*/
	/*	0, 1, 2, 79, 2537, 9900, 9901, 9902, 9904};*/

	p = hdr10_plus_sei;
	pvf = &hdr10p_sei;

	hdr10_plus_sei->num_distributions[0] = PERCENTILE_ORDER - 1;

	for (i = 0; i < 3; i++)
		hdr10_plus_sei->maxscl[0][i] = hdr10p_sei.maxscl[0][i];

	hdr10_plus_sei->targeted_system_display_maximum_luminance =
		hdr10p_sei.tgt_sys_disp_max_lumi;

	/*for hdmitx output no number, default 9*/
	if (hdr10_plus_sei->num_distributions[0] == 0)
		hdr10_plus_sei->num_distributions[0] = 9;

	for (i = 0; i < hdr10_plus_sei->num_distributions[0]; i++) {
		hdr10_plus_sei->distribution_index[0][i] =
			hdr10p_sei.distribution_maxrgb_percentages[0][i];
		/*hdmitx have no distribution index, default init*/
		if (hdr10_plus_sei->distribution_index[0][i] == 0)
			hdr10_plus_sei->distribution_index[0][i] =
				percentilepercent_init[i];

		hdr10_plus_sei->distribution_values[0][i] =
			hdr10p_sei.distribution_maxrgb_percentiles[0][i];
	}

	hdr10_plus_sei->knee_point_x[0] = hdr10p_sei.knee_point_x[0];
	hdr10_plus_sei->knee_point_y[0] = hdr10p_sei.knee_point_y[0];

	if (hdr10p_sei.num_bezier_curve_anchors[0]) {
		hdr10_plus_sei->num_bezier_curve_anchors[0] =
			hdr10p_sei.num_bezier_curve_anchors[0];
		for (i = 0;
			i < hdr10_plus_sei->num_bezier_curve_anchors[0];
			i++)
			hdr10_plus_sei->bezier_curve_anchors[0][i] =
				hdr10p_sei.bezier_curve_anchors[0][i] <<
				(PROCESSING_MAX - 10);
	} else {
		hdr10_plus_sei->num_bezier_curve_anchors[0] = 9;
		for (i = 0; i < 9; i++)
			hdr10_plus_sei->bezier_curve_anchors[0][i] =
				p_init[i] << (PROCESSING_MAX - 10);
	}

	/*debug--*/
	if (hdr10p_printk & 1) {
		for (i = 0; i < 3; i++)
			pr_hdr("hdr10_plus_sei->maxscl[0][%d]=%d\n",
				i, hdr10_plus_sei->maxscl[0][i]);

		pr_hdr("targeted_system_display_maximum_luminance=%d\n",
			p->targeted_system_display_maximum_luminance);

		for (i = 0; i < hdr10_plus_sei->num_distributions[0]; i++) {
			pr_hdr("distribution_values[0][%d]=%d\n",
				i, hdr10_plus_sei->distribution_values[0][i]);
			pr_hdr("hdr10_plus_sei->distribution_index[0][%d]=%d\n",
				i, hdr10_plus_sei->distribution_index[0][i]);
		}

		pr_hdr("hdr10_plus_sei->knee_point_x = %d\n"
			"hdr10_plus_sei->knee_point_y = %d\n",
			hdr10_plus_sei->knee_point_x[0],
			hdr10_plus_sei->knee_point_y[0]);

		pr_hdr("hdr10_plus_sei->num_bezier_curve_anchors[0] = %d\n",
			hdr10p_sei.num_bezier_curve_anchors[0]);

		for (i = 0; i < hdr10_plus_sei->num_bezier_curve_anchors[0];
			i++)
			pr_hdr("bezier_curve_anchors[0][%d] = %d\n",
				i, hdr10p_sei.bezier_curve_anchors[0][i]);

		for (i = 0; i < 3; i++)
			pr_hdr("average_maxrgb[%d] = %d\n",
				i, hdr10p_sei.average_maxrgb[i]);

		for (i = 0;
			i < pvf->num_distribution_maxrgb_percentiles[0];
			i++)
			pr_hdr("distribution_maxrgb_percentages[0][%d] = %d\n",
				i, pvf->distribution_maxrgb_percentages[0][i]);

		for (i = 0;
			i < pvf->num_distribution_maxrgb_percentiles[0];
			i++)
			pr_hdr("distribution_maxrgb_percentiles[0][%d] = %d\n",
				i, pvf->distribution_maxrgb_percentiles[0][i]);
	}
}

static void vframe_hdr_sei_s_init(struct hdr10_plus_sei_s *hdr10_plus_sei,
	int source_lumin, u32 *percentile)
{
	int i;
	int p_init[N - 1] = {181, 406, 607, 796, 834, 863, 890, 917, 938};
	int percentilepercent_init[PERCENTILE_ORDER] = {
		1, 5, 10, 25, 50, 75, 90, 95, 99
	};
	int percentilevalue_init[PERCENTILE_ORDER] = {
		0, 10000, 90, 2000, 4000, 8000, 10000, 10001, 10002
	};

	for (i = 0; i < 9; i++)
		percentilevalue_init[i] = percentile[i];

	if (hdr10p_printk & 8)
		for (i = 0; i < 9; i++)
			pr_hdr("percentilevalue_init[%d] = %d\n",
				i, percentilevalue_init[i]);

	hdr10_plus_sei->num_distributions[0] = PERCENTILE_ORDER - 1;

	for (i = 0; i < 3; i++)
		hdr10_plus_sei->maxscl[0][i] = percentilevalue_init[8] * 10;

	hdr10_plus_sei->targeted_system_display_maximum_luminance = 0;

	hdr10_plus_sei->num_distributions[0] = 9;
	for (i = 0; i < hdr10_plus_sei->num_distributions[0]; i++) {
		hdr10_plus_sei->distribution_index[0][i] =
			percentilepercent_init[i];
		hdr10_plus_sei->distribution_values[0][i] =
			percentilevalue_init[i];
	}

	hdr10_plus_sei->knee_point_x[0] = 0;
	hdr10_plus_sei->knee_point_y[0] = 0;

	hdr10_plus_sei->num_bezier_curve_anchors[0] = 9;
	for (i = 0; i < 9; i++)
		hdr10_plus_sei->bezier_curve_anchors[0][i] =
			p_init[i] << (PROCESSING_MAX - 10);

	for (i = 0; i < 3; i++)
		hdr10p_sei.average_maxrgb[i] = percentilevalue_init[8];

	hdr10p_sei.num_distribution_maxrgb_percentiles[0] = 0;
}

static unsigned int gain[POINTS];
static u64 curvex[POINTS];
static u64 curvey[POINTS];

/*input o->10000, should adaptive scale by shift and gamut*/
static int match_ootf_output(struct scene2094metadata *metadata,
	struct hdr10pgen_param_s *p_hdr10pgen_param,
	int panel_lumin)
{
	unsigned int scale_float;
	unsigned int maxl;

	maxl = metadata->maxscenesourceluminance;
	if (!maxl)
		maxl = 1000;
	if (maxl < panel_lumin)
		maxl = panel_lumin;

	/*o->10000 scale*/
	scale_float = 10000 * 1024 / maxl;

	p_hdr10pgen_param->shift = _log2(scale_float) - 10;
	/*right shifting by more than 31 bits has undefined behavior.*/
	if (p_hdr10pgen_param->shift < 32)
		p_hdr10pgen_param->scale_gmt =
			scale_float >> p_hdr10pgen_param->shift;

	memcpy(p_hdr10pgen_param->gain, gain, sizeof(unsigned int) * POINTS);

	if (hdr10p_printk & 4) {
		pr_hdr("maxl=%d, scale=%d, shift=%d, ",
			maxl, scale_float,
			p_hdr10pgen_param->shift);
		pr_hdr("scale_gmt=%d, gain=%d-%d-%d\n",
			p_hdr10pgen_param->scale_gmt,
			gain[0], gain[86], gain[148]);
	}

	return 0;
}

/*mapping*/
/*struct hdr_proc_lut_param_s _hdr_lut_param;*/

int hdr10p_ootf_gen(int panel_lumin,
	int force_source_lumin,
	struct hdr10pgen_param_s *p_hdr10pgen_param,
	u32 *percentile)
{
	/*int referenceCurve_flag = 1;*/
	int order, i;
	u64 kx, ky;
	u64 anchor[N];

	/* bezier params obtained from metadata */
	static struct hdr10_plus_sei_s hdr10_plus_sei;
	static struct scene2094metadata metadata;
	static struct ebzcurveparameters referencebezierparams;
	static struct ebzcurveparameters productbezierparams;
	static struct basisootf_params basisootf_params;

	int productpeak = panel_lumin;

	memset(curvex, 0, sizeof(uint64_t) * POINTS);
	memset(curvey, 0, sizeof(uint64_t) * POINTS);
	memset(gain, 0, sizeof(unsigned int) * POINTS);

	hdr10p_basisootf_params_init(&basisootf_params);

	/*the final tv OOTF curve params init*/
	ebzcurveparametersinit(&productbezierparams);

	/*the bezier parameters from metadata init*/
	ebzcurveparametersinit(&referencebezierparams);

	/*repace with real vframe data*/
	if (!force_source_lumin)
		vframe_hdr_plus_sei_s_init(&hdr10_plus_sei);
	else
		vframe_hdr_sei_s_init(&hdr10_plus_sei,
			force_source_lumin, percentile);

	/*step 1. get metadata from vframe*/
	metadatainit(&metadata, &hdr10_plus_sei);

	/*step 2. get bezier params from metadata*/
	getmetadata(&metadata, &referencebezierparams);

	/*step 3. gen final guided OOTF*/
	/*if (referenceCurve_flag == 0)*/
		/* Basis OOTF : Direct calculation of product TM curve from*/
		/*ST-2094 percentile metadata */
		/*basisOOTF(&metadata, &basisOOTF_Params, productPeak,*/
		/* here  length(minBezierParams->Anchor) =order*/
		/*metadata.maxscenesourceluminance, &productBezierParams);*/
	/*else*/

	guidedootf(&metadata, &basisootf_params,
		&referencebezierparams, productpeak,
		&productbezierparams);

	if (hdr10p_printk & 2) {
		pr_hdr("productPeak=%d\n", panel_lumin);
		pr_hdr("\n===================guided out ===================\n");
		pr_hdr("1. knee point : (%4d,%4d)\n",
			productbezierparams.sx, productbezierparams.sy);
		pr_hdr("2. Bezier order : %d\n",
			referencebezierparams.order);
		pr_hdr("3. Bezier P coeffs :\n");
		for (i = 0; i < productbezierparams.order - 1; i++)
			pr_hdr(" %2d: %4d\n",
				i, productbezierparams.anchor[i]);
		pr_hdr("\n=================================================\n");
	}

	/*step 4. get guided bezier params*/
	kx = (uint64_t)productbezierparams.sx;
	ky = (uint64_t)productbezierparams.sy;
	order = productbezierparams.order;
	for (i = 0; i < productbezierparams.order - 1; i++)
		anchor[i] = (uint64_t)productbezierparams.anchor[i];

	/*step 5. gen bezier curve*/
	gen_ebzurve(&curvex[0], &curvey[0], &gain[0],
		kx, ky, &anchor[0], order);

	/*debug*/
	if (hdr10p_printk & 8)
		for (i = 0; i < POINTS; i++)
			pr_hdr("fixed : %3d:(%lld, %lld)->%4d\n",
				i, curvex[i], curvey[i], gain[i]);

	match_ootf_output(&metadata, p_hdr10pgen_param, panel_lumin);
	hdr10p_printk = 0;

	return 0;
}

