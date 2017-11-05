#ifndef HSP_H
#define HSP_H

namespace detail
{
static const double Pr=.299;
static const double Pg=.587;
static const double Pb=.114;

//  public domain function by Darel Rex Finley, 2006
//
//  This function expects the passed-in values to be on a scale
//  of 0 to 1, and uses that same scale for the return values.
//
//  See description/examples at alienryderflex.com/hsp.html

template<class value_type>
void RGBtoHSP(value_type R, value_type G, value_type B, value_type *H, value_type *S, value_type *P) {
	//  Calculate the Perceived brightness.
	*P = sqrt(R * R * Pr + G * G * Pg + B * B * Pb);

	//  Calculate the Hue and Saturation.  (This part works
	//  the same way as in the HSV/B and HSL systems???.)
	if (R == G && R == B) {
		*H = 0.;
		*S = 0.;
		return;
	}
	if (R >= G && R >= B) { //  R is largest
		if (B >= G) {
			*H = 6. / 6. - 1. / 6. * (B - G) / (R - G);
			*S = 1. - G / R;
		} else {
			*H = 0. / 6. + 1. / 6. * (G - B) / (R - B);
			*S = 1. - B / R;
		}
	} else if (G >= R && G >= B) { //  G is largest
		if (R >= B) {
			*H = 2. / 6. - 1. / 6. * (R - B) / (G - B);
			*S = 1. - B / G;
		} else {
			*H = 2. / 6. + 1. / 6. * (B - R) / (G - R);
			*S = 1. - R / G;
		}
	} else { //  B is largest
		if (G >= R) {
			*H = 4. / 6. - 1. / 6. * (G - R) / (B - R);
			*S = 1. - R / B;
		} else {
			*H = 4. / 6. + 1. / 6. * (R - G) / (B - G);
			*S = 1. - G / B;
		}
	}
}

//  public domain function by Darel Rex Finley, 2006
//
//  This function expects the passed-in values to be on a scale
//  of 0 to 1, and uses that same scale for the return values.
//
//  Note that some combinations of HSP, even if in the scale
//  0-1, may return RGB values that exceed a value of 1.  For
//  example, if you pass in the HSP color 0,1,1, the result
//  will be the RGB color 2.037,0,0.
//
//  See description/examples at alienryderflex.com/hsp.html

template<class value_type>
void HSPtoRGB(value_type H, value_type S, value_type P, value_type *R, value_type *G, value_type *B) {

	value_type part, minOverMax = 1. - S;

	if (minOverMax > 0.) {
		if (H < 1. / 6.) { //  R>G>B
			H = 6. * (H - 0. / 6.);
			part = 1. + H * (1. / minOverMax - 1.);
			*B = P / sqrt(Pr / minOverMax / minOverMax + Pg * part * part + Pb);
			*R = (*B) / minOverMax;
			*G = (*B) + H * ((*R) - (*B));
		} else if (H < 2. / 6.) { //  G>R>B
			H = 6. * (-H + 2. / 6.);
			part = 1. + H * (1. / minOverMax - 1.);
			*B = P / sqrt(Pg / minOverMax / minOverMax + Pr * part * part + Pb);
			*G = (*B) / minOverMax;
			*R = (*B) + H * ((*G) - (*B));
		} else if (H < 3. / 6.) { //  G>B>R
			H = 6. * (H - 2. / 6.);
			part = 1. + H * (1. / minOverMax - 1.);
			*R = P / sqrt(Pg / minOverMax / minOverMax + Pb * part * part + Pr);
			*G = (*R) / minOverMax;
			*B = (*R) + H * ((*G) - (*R));
		} else if (H < 4. / 6.) { //  B>G>R
			H = 6. * (-H + 4. / 6.);
			part = 1. + H * (1. / minOverMax - 1.);
			*R = P / sqrt(Pb / minOverMax / minOverMax + Pg * part * part + Pr);
			*B = (*R) / minOverMax;
			*G = (*R) + H * ((*B) - (*R));
		} else if (H < 5. / 6.) { //  B>R>G
			H = 6. * (H - 4. / 6.);
			part = 1. + H * (1. / minOverMax - 1.);
			*G = P / sqrt(Pb / minOverMax / minOverMax + Pr * part * part + Pg);
			*B = (*G) / minOverMax;
			*R = (*G) + H * ((*B) - (*G));
		} else { //  R>B>G
			H = 6. * (-H + 6. / 6.);
			part = 1. + H * (1. / minOverMax - 1.);
			*G = P / sqrt(Pr / minOverMax / minOverMax + Pb * part * part + Pg);
			*R = (*G) / minOverMax;
			*B = (*G) + H * ((*R) - (*G));
		}
	} else {
		if (H < 1. / 6.) { //  R>G>B
			H = 6. * (H - 0. / 6.);
			*R = sqrt(P * P / (Pr + Pg * H * H));
			*G = (*R) * H;
			*B = 0.;
		} else if (H < 2. / 6.) { //  G>R>B
			H = 6. * (-H + 2. / 6.);
			*G = sqrt(P * P / (Pg + Pr * H * H));
			*R = (*G) * H;
			*B = 0.;
		} else if (H < 3. / 6.) { //  G>B>R
			H = 6. * (H - 2. / 6.);
			*G = sqrt(P * P / (Pg + Pb * H * H));
			*B = (*G) * H;
			*R = 0.;
		} else if (H < 4. / 6.) { //  B>G>R
			H = 6. * (-H + 4. / 6.);
			*B = sqrt(P * P / (Pb + Pg * H * H));
			*G = (*B) * H;
			*R = 0.;
		} else if (H < 5. / 6.) { //  B>R>G
			H = 6. * (H - 4. / 6.);
			*B = sqrt(P * P / (Pb + Pr * H * H));
			*R = (*B) * H;
			*G = 0.;
		} else { //  R>B>G
			H = 6. * (-H + 6. / 6.);
			*R = sqrt(P * P / (Pr + Pb * H * H));
			*B = (*R) * H;
			*G = 0.;
		}
	}
}

}

template<class value_type>
void rgb_to_hsp(const std::array<value_type, 3> &rgb, std::array<value_type, 3> &hsp)
{
	detail::RGBtoHSP(rgb[0], rgb[1], rgb[2], &hsp[0], &hsp[1], &hsp[2]);
}

template<class value_type>
void hsp_to_rgb(const std::array<value_type, 3> &hsp, std::array<value_type, 3> &rgb)
{
	detail::HSPtoRGB(hsp[0], hsp[1], hsp[2], &rgb[0], &rgb[1], &rgb[2]);
}

template<class value_type>
std::array<value_type, 3> rgb_to_hsp(const std::array<value_type, 3> &rgb)
{
	std::array<value_type, 3> hsp;

	rgb_to_hsp(rgb, hsp);

	return hsp;
}

template<class value_type>
std::array<value_type, 3> hsp_to_rgb(const std::array<value_type, 3> &hsp)
{
	std::array<value_type, 3> rgb;
	
	hsp_to_rgb(hsp, rgb);

	return rgb;
}

#endif /* HSP_H */
