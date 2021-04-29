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
	__VERIFIER_assume(N <= 2147483647/sizeof(int));

	int i, j;
	int S[1];
	int A[N];

	for (i = 0; i < N; i++)
	{
		A[i] = 0;
	}

	S[0] = 0;
	for (i = 0; i < N; i++)
	{
		S[0] = S[0] + 1;
	}

	for (i = 0; i < N; i++)
	{
		for (j = 0; j < N; j++)
		{
			A[i] = A[i] + 1;
		}
		A[i] = A[i] + S[0];
	}

	int x;
	for (x = 0; x < N; x++)
	{
		__VERIFIER_assert(A[x] == 2*N);
	}
}

