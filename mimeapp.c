
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <fcntl.h>
#include "mimeapp.h"

char* strchrnul(const char*, int);


struct Application get_default_mime_app(const char* mimetype) {
	struct Application a = {0,0};
	int pipes[2];
	char buffer[64] = {0};
	int n;

	pipe(pipes);
	fcntl(pipes[1], F_SETFL, fcntl(pipes[1], F_GETFL) | O_NONBLOCK);
	fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL) | O_NONBLOCK);

	pid_t pid = fork();
	if(pid == 0) { //child
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		execlp("xdg-mime", "xdg-mime", "query", "default", mimetype, NULL);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	printf("preread\n");
	n = read(pipes[0], buffer, 63);
	printf("postread: %d\n", n);
	if(n < 0) return a;
	buffer[n] = '\0';
	*strchrnul(buffer, ';') = '\0';
	*strchrnul(buffer, '\n') = '\0';
	char* path = malloc(sizeof("/usr/share/applications/") + strlen(buffer) + 1);
	sprintf(path, "/usr/share/applications/%s", buffer);

	pid = fork();
	if(pid == 0) {
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		execlp("grep", "grep", "^Exec=", path, NULL);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	n = read(pipes[0], buffer, 63);
	if(n < 0) return a;
	buffer[n] = '\0';
	*strchrnul(buffer, '\n') = '\0';
	char executable_path[64] = {0};
	sscanf(buffer, "Exec=%s", executable_path);

	pid = fork();
	if(pid == 0) {
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		execlp("grep", "grep", "^Name=", path, NULL);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	n = read(pipes[0], buffer, 63);
	if(n < 0) return a;
	buffer[n] = '\0';
	*strchrnul(buffer, '\n') = '\0';
	char *executable_name = &buffer[sizeof("Name=")-1];

	free(path);
	close(pipes[0]);
	close(pipes[1]);

	a.name = strdup(executable_name);
	a.exec = strdup(executable_path);


	return a;
}

