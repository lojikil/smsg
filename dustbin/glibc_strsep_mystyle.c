#include <string.h>

char *strsep(char **p, const char *delim)
{
	char *begin, *end;

	if((begin = *p) == NULL) return NULL;

	//a frequent case is when the delimiter string contains only one character
	//here we don't need to call the expensive strpbrk() function and instead work using strchr()
	if(delim[0] == '\0' || delim[1] == '\0') {
		char ch = delim[0];
		if(ch == '\0') {
			end = NULL;
		} else {
			if(*begin == ch) end = begin;
			else if(*begin == '\0') end = NULL;
			else end = strchr(begin+1, ch);
		}
	} else {
		end = strpbrk(begin, delim);	//find the end of the token
	}

	if(end) {
		//terminate the token and set *p past NUL character
		*end++ = '\0';
		*p = end;
	} else {
		*p = NULL;	//no more delimiters; this is the last token
	}

	return begin;
}
