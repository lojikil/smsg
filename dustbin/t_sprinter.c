char *lcc_sprinter(const char *fmt, ...)
{
	int n, size = 100;	//start off with 100 bytes
	char *p;
	va_list ap;

	if((p = malloc (size)) == NULL) return NULL;
	while(1) {
		//try to print into current buffer
		va_start(ap, fmt);
		n = vsnprintf (p, size, fmt, ap);
		va_end(ap);
		if(n > -1 && n < size) return p;	//if it fits, return it...we're done
		//otherwise allocate more space
		if(n > -1) size = n+1;	//glibc 2.1, allocate precisely what is needed
		else size *= 2;		//glibc 2.0, twice the old size
		if((p = realloc (p, size)) == NULL) return NULL;
	}
}
