/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <sys/wait.h>
#include <fcntl.h>
#include "exec.h"

ssize_t exec_get(char* buffer, size_t nbuf, const char* file, const char* const* args) {
	int pipes[2];

	pipe(pipes);
	fcntl(pipes[1], F_SETFL, fcntl(pipes[1], F_GETFL) | O_NONBLOCK);
	fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL) | O_NONBLOCK);

	pid_t pid = fork();
	if(pid == 0) { //child
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		execvp(file, (char*const*)args);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	ssize_t n = read(pipes[0], buffer, nbuf);
	if(n < 0)
		return (int) n;
	buffer[nbuf] = '\0';

	close(pipes[0]);
	close(pipes[1]);

	return n;
}


