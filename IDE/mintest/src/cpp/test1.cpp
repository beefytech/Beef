#include <stdio.h>

int gVal1 = 123;
int gVal2 = 0;
int gVal3;

template <typename T>
void Poofie(T a)
{
	printf("2\n");
}

void Fart()
{
	Poofie<int>(234);
}