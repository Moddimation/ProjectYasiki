
#ifndef _No_Floating_Point  
/* @(#)s_asinh.c 1.2 95/01/04 */
/* $Id: s_asinh.c,v 1.2.4.1 1999/12/06 19:47:53 fassiott Exp $ */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/* asinh(x)
 * Method :
 *	Based on 
 *		asinh(x) = sign(x) * log [ |x| + sqrt(x*x+1) ]
 *	we have
 *	asinh(x) := x  if  1+x*x=1,
 *		 := sign(x)*(log(x)+ln2)) for large |x|, else
 *		 := sign(x)*log(2|x|+1/(|x|+sqrt(x*x+1))) if|x|>2, else
 *		 := sign(x)*log1p(|x| + x^2/(1 + sqrt(1+x^2)))  
 */

#include "fdlibm.h"

#ifdef __STDC__
static const double 
#else
static double 
#endif
one =  1.00000000000000000000e+00, /* 0x3FF00000, 0x00000000 */
ln2 =  6.93147180559945286227e-01, /* 0x3FE62E42, 0xFEFA39EF */
big =  1.00000000000000000000e+300; 

#ifdef __STDC__
	double asinh(double x)
#else
	double asinh(x)
	double x;
#endif
{	
	double t,w;
	int hx,ix;
	hx = __HI(x);
	ix = hx&0x7fffffff;
	if(ix>=0x7ff00000) return x+x;	/* x is inf or NaN */
	if(ix< 0x3e300000) {	/* |x|<2**-28 */
	    if(big+x>one) return x;	/* return x inexact except 0 */
	} 
	if(ix>0x41b00000) {	/* |x| > 2**28 */
	    w = __ieee754_log(fabs(x))+ln2;
	} else if (ix>0x40000000) {	/* 2**28 > |x| > 2.0 */
	    t = fabs(x);
	    w = __ieee754_log(2.0*t+one/(sqrt(x*x+one)+t));
	} else {		/* 2.0 > |x| > 2**-28 */
	    t = x*x;
	    w =log1p(fabs(x)+t/(one+sqrt(one+t)));
	}
	if(hx>0) return w; else return -w;
}
#endif /* _No_Floating_Point  */
