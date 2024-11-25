//=======================================================================
//			Copyright (C) XashXT Group 2011
//		         stringlib.h - safety string routines 
//=======================================================================
#pragma once

#include <string.h>
#include <stdio.h>

char *Q_pretifymem( float value, int digitsafterdecimal );

#define Q_memprint( val) Q_pretifymem( val, 2 )
