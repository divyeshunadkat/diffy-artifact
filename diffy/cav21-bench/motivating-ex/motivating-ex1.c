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
	int f[1];
	int sum[1];
	int i;

	sum[0] = 0;
	for(i = 0; i< N; i++) 
	{ 
		a[i] = __VERIFIER_nondet_int();
	}

	for(i = 0; i< N; i++) {
		if ( a[i] >= 0 ) b[i] = 1;
		else b[i] = 0;
		sum[0] = sum[0] + 1;
	}

	f[0] = 1;
	for(i = 0; i< N; i++) {
		if(sum[0] == N) { 
			if ( a[i] >= 0 && !b[i] ) f[0] = 0;
			if ( a[i] < 0 && b[i] ) f[0] = 0;
		}
	}

	__VERIFIER_assert ( f[0] == 1 ); 
	return 0;
}
