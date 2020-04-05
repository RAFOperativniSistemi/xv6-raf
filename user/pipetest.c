#include "kernel/types.h"
#include "user.h"

int
main(void)
{
	int p[2];					// 0 je in, 1 je out
	char *child_argv[] = {"wc", 0};

	pipe(p);					// kreiramo pipe

	if(fork() == 0) {				// u child procesu
		close(0);				// zatvorimo stari stdin - /dev/console
		dup(p[0]);				// postavimo p[0] kao stdin

		close(p[0]);				// zatvorimo pipe, ne treba nam vise u child procesu
		close(p[1]);

		exec("/bin/wc", child_argv);		// pokrenemo wc kao child - njegov stdin ce biti vezan na pipe
	} else {					// u roditeljskom procesu
		close(p[0]);				// ne treba nam in kraj pipe-a
		write(p[1], "hello world\n", 12);	// pisemo na out kraj - ovo ide u wc kao da dolazi sa tastature
		close(p[1]);				// zatvorimo pipe
	}

	wait();						// cekamo da se child zavrsi - sta se desava ako zakomentarisemo ovo?

	exit();
}
