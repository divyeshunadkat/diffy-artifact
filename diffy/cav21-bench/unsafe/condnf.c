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

int main()
{
	N = __VERIFIER_nondet_int();
	if(N <= 0) return 1;

	int i;
	int a[N];

	for(i=0; i<N; i++)
	{
		a[i] = __VERIFIER_nondet_int();
	}

	for(i=0; i<N; i++)
	{
		if(a[i] > N) {
			a[i] = a[i];
		} else {
			a[i] = N;
		}
	}

	for(i=0; i<N; i++)
	{
		__VERIFIER_assert(a[i] <= N);
	}
	return 1;
}
