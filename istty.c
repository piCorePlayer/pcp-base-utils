// pcpget.c
#include <unistd.h>

int main(int argc, char **argv) {
	if (isatty(1))
		return 0;
	else
		return 1;
}
