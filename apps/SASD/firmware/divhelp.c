/* 32-bit multiply/divide helpers for the 68000 (the link is done with -nostdlib) */
unsigned long __udivsi3(unsigned long n, unsigned long d)
{
	unsigned long q = 0, r = 0;
	int i;
	if (!d) return 0xffffffffUL;
	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) { r -= d; q |= 1UL << i; }
	}
	return q;
}
unsigned long __umodsi3(unsigned long n, unsigned long d)
{
	unsigned long r = 0;
	int i;
	if (!d) return n;
	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) r -= d;
	}
	return r;
}
unsigned long __mulsi3(unsigned long a, unsigned long b)
{
	unsigned long r = 0;
	while (b) {
		if (b & 1) r += a;
		a <<= 1;
		b >>= 1;
	}
	return r;
}
