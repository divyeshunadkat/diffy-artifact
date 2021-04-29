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

	int b[N]; 
	int x[1];
	int i, j;

	x[0] = 0;
	for(i = 0; i < N; i++) 
	{ 
		x[0] = x[0] + i;
	}

	for(j = 0; j < N; j++) {
		b[j] = x[0] + N;
	}

	for(j = 0; j < N; j++) {
		__VERIFIER_assert ( b[j] == N*(N-1)/2 + N); 
	}

	return 0;
}
