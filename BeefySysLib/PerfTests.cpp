#include "Common.h"



/*int main(int, char** argv)
{
	int i, n = atoi(argv[1]);
	N_Body_System system;

	printf("%.9f\n", system.energy());
	for (i = 0; i < n; ++i)
		system.advance(0.01);
	printf("%.9f\n", system.energy());

	return 0;
}*/

void NBody(int n);
void FastaRedux(int n);
void FannkuchRedux(int max_n);

USING_NS_BF;

BF_EXPORT void BF_CALLTYPE BFApp_RunPerfTest(const char* testName, int arg)
{
	if (strcmp(testName, "nbody") == 0)
		NBody(arg);
	if (strcmp(testName, "fastaredux") == 0)
		FastaRedux(arg);
	if (strcmp(testName, "fannkuchredux") == 0)
		FannkuchRedux(arg);
}
