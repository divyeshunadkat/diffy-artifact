/*
 * Benchmarks contributed by Divyesh Unadkat[1,2], Supratik Chakraborty[1], Ashutosh Gupta[1]
 * [1] Indian Institute of Technology Bombay, Mumbai
 * [2] TCS Innovation labs, Pune
 *
 */

extern void __VERIFIER_error() __attribute__ ((__noreturn__));
extern void __VERIFIER_assume(int);
void __VERIFIER_assert(int cond) { if(!(cond)) { ERROR: __VERIFIER_error(); } }
extern int __VERIFIER_nondet_int();

int N;

int main ( ) {
	N = __VERIFIER_nondet_int();
	if(N <= 0) return 1;
	__VERIFIER_assume(N <= 2147483647/sizeof(int));

	int a[N]; 
	int b[N]; 
	int x[1];
	int i;

	for(i = 0; i < N; i++)
	{
		a[i] = 1;
	}

	x[0] = 0;
	for(i = 0; i < N; i++) 
	{ 
		x[0] = x[0] + a[i];
	}

	for(i = 0; i < N; i++)
	{
		b[i] = x[0] + N;
	}

	x[0] = 0;
	for(i = 0; i < N; i++) 
	{ 
		x[0] = x[0] + b[i];
	}

	__VERIFIER_assert ( x[0] == 2*N*N); 
	return 0;
}
