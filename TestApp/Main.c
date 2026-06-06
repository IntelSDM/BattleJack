#include <Windows.h>
#include <stdio.h>
int main(void)
{
	printf("Hello, world!\n");
	MessageBoxA(NULL, "Hello, world!", "TestApp", MB_OK);
	return 0;
}