/*
 * Benchmarks contributed by Divyesh Unadkat[1,2], Supratik Chakraborty[1], Ashutosh Gupta[1]
 * [1] Indian Institute of Technology Bombay, Mumbai
 * [2] TCS Innovation labs, Pune
 *
 */

extern void __VERIFIER_error() __attribute__ ((__noreturn__));
extern void __VERIFIER_assume(int);
void __VERIFIER_assert(int cond) { if(!(cond)) { ERROR: __VERIFIER_error(); } }
extern int __VERIFIER_nondet_int(void);

int N;

int main ( ) {
	N = __VERIFIER_nondet_int();
	if(N <= 0) return 1;
	__VERIFIER_assume(N <= 2147483647/sizeof(int));

	int a [N];
	int b [N];
	int incr[1];
	int sum[1];
	int i = 0;

       	incr[0] =  __VERIFIER_nondet_int();
	while ( i < N ) {
		a[i] = 42;
		i = i + 1;
	}

	sum[0] = 0; 
	for ( i = 0 ; i < N ; i++ ) {
		b[i] = a[i];
		sum[0] = sum[0] + 1;
	}

	for ( i = 0 ; i < N ; i++ ) {
		if(sum[0] == N) {
			b[i] = b[i] + incr[0];
		} else {
			b[i] = a[i] + incr[0];
		}
	}

	int x;
	for ( x = 0 ; x < N ; x++ ) {
		__VERIFIER_assert( b[x] == 44 + incr[0] );
	}
	return 0;
}
