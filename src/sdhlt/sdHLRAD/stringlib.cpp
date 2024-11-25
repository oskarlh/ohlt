//=======================================================================
//			Copyright (C) XashXT Group 2011
//		         stringlib.cpp - safety string routines 
//=======================================================================

#ifdef _WIN32
#include <windows.h>
#endif
#include <math.h>
#include "stringlib.h"
#ifdef _WIN32
#include <direct.h>
#endif
#include "stringlib.h"
#include "cmdlib.h"
#define Q_max(a, b) ((a) > (b) ? (a) : (b))


int Q_strlen( const char *string )
{
	if( !string ) return 0;

	int len = 0;
	const char *p = string;
	while( *p )
	{
		p++;
		len++;
	}
	return len;
}



int Q_strncmp( const char *s1, const char *s2, int n )
{
	int		c1, c2;

	if( s1 == nullptr )
	{
		if( s2 == nullptr ) return 0;
		else return -1;
	}
	else if( s2 == nullptr )
		return 1;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		// strings are equal until end point
		if( !n-- ) return 0;
		if( c1 != c2 ) return c1 < c2 ? -1 : 1;

	} while( c1 );
	
	// strings are equal
	return 0;
}

char *Q_strstr( const char *string, const char *string2 )
{
	int	c, len;

	if( !string || !string2 ) return nullptr;

	c = *string2;
	len = Q_strlen( string2 );

	while( string )
	{
		for( ; *string && *string != c; string++ );

		if( *string )
		{
			if( !Q_strncmp( string, string2, len ))
				break;
			string++;
		}
		else return nullptr;
	}
	return (char *)string;
}

int Q_vsnprintf( char *buffer, size_t buffersize, const char *format, va_list args )
{
	size_t	result;

	result = vsnprintf( buffer, buffersize, format, args );

	if( result < 0 || result >= buffersize )
	{
		buffer[buffersize - 1] = '\0';
		return -1;
	}
	return result;
}

int Q_snprintf( char *buffer, size_t buffersize, const char *format, ... )
{
	va_list	args;
	int	result;

	va_start( args, format );
	result = Q_vsnprintf( buffer, buffersize, format, args );
	va_end( args );

	return result;
}

int Q_sprintf( char *buffer, const char *format, ... )
{
	va_list	args;
	int	result;

	va_start( args, format );
	result = Q_vsnprintf( buffer, 99999, format, args );
	va_end( args );

	return result;
}


char *Q_pretifymem( float value, int digitsafterdecimal )
{
	static char	output[8][32];
	static int	current;
	float		onekb = 1024.0f;
	float		onemb = onekb * onekb;
	char		suffix[8];
	char		*out = output[current];
	char		val[32], *i, *o, *dot;
	int		pos;

	current = ( current + 1 ) & ( 8 - 1 );

	// first figure out which bin to use
	if( value > onemb )
	{
		value /= onemb;
		Q_sprintf( suffix, " Mb" );
	}
	else if( value > onekb )
	{
		value /= onekb;
		Q_sprintf( suffix, " Kb" );
	}
	else Q_sprintf( suffix, " bytes" );

	// clamp to >= 0
	digitsafterdecimal = Q_max( digitsafterdecimal, 0 );

	// if it's basically integral, don't do any decimals
	if( fabs( value - (int)value ) < 0.00001 )
	{
		Q_sprintf( val, "%i%s", (int)value, suffix );
	}
	else
	{
		char fmt[32];

		// otherwise, create a format string for the decimals
		Q_sprintf( fmt, "%%.%if%s", digitsafterdecimal, suffix );
		Q_sprintf( val, fmt, value );
	}

	// copy from in to out
	i = val;
	o = out;

	// search for decimal or if it was integral, find the space after the raw number
	dot = Q_strstr( i, "." );
	if( !dot ) dot = Q_strstr( i, " " );

	pos = dot - i;	// compute position of dot
	pos -= 3;		// don't put a comma if it's <= 3 long

	while( *i )
	{
		// if pos is still valid then insert a comma every third digit, except if we would be
		// putting one in the first spot
		if( pos >= 0 && !( pos % 3 ))
		{
			// never in first spot
			if( o != out ) *o++ = ',';
		}

		pos--;		// count down comma position
		*o++ = *i++;	// copy rest of data as normal
	}
	*o = 0; // terminate

	return out;
}


/*
==============
COM_IsSingleChar

interpert this character as single
==============
*/
static int COM_IsSingleChar( char c )
{
	if( c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ',' )
		return true;
	return false;
}
