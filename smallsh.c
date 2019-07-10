#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFF_SIZE 2048

typedef char bool;

typedef struct Command {
	char* args[512];
	int inputFile;
	int outputFile;
	bool isBackground;
} Command;

char status[24];

void setInputFile(Command* command, char* filename) {
	/*Use next argument as file name. Open read only.*/
	command->inputFile = open(filename, O_RDONLY);
	/*Close on exec*/
	fcntl(command->inputFile, F_SETFD, FD_CLOEXEC);
}

void setOutputFile(Command* command, char* filename) {
	/*Use next argument as file name. Open write only. Create if
	  file doesn't exist. Overwrite if file exists. rw-r--r-- */
	command->outputFile = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	/*Close on exec*/
	fcntl(command->outputFile, F_SETFD, FD_CLOEXEC);
}

short redirectError(char* filename, bool isBackground) {
	fprintf(stderr, "%s%s\n", "cannot open ", filename);
	fflush(stderr);
	if (!isBackground) {
		sprintf(status, "%s", "exit value 1");
	}
	return -1;
}

short redirectionCheck(Command* command, int argNum) {
	int i;
	for (i = 0; i < argNum; i++) {
		if (!command->args[i]) {
			continue;
		}
		
		if (!strcmp(command->args[i], "<")) {
			/*Set to null since command args must come before redirection*/
			command->args[i] = 0;
			setInputFile(command, command->args[i+1]);
			if (command->inputFile < 0) {
				return redirectError(command->args[i+1],
									 command->isBackground);
			}
		} else if (!strcmp(command->args[i], ">")) {
			/*Set to null since command args must come before redirection*/
			command->args[i] = 0;
			setOutputFile(command, command->args[i+1]);
			if (command->outputFile < 0) {
				return redirectError(command->args[i+1],
									 command->isBackground);
			}
		}
	}

	return 0;
}

short backgroundCheck(Command* command, int argNum) {
	if (!strcmp(command->args[argNum-1], "&")) {
		/*Set to null since no longer needed*/
		command->args[argNum-1] = 0;
		command->isBackground = 1;
		/*Redirect to /dev/null*/
		command->inputFile = open("/dev/null", O_RDONLY);
		command->outputFile = open("/dev/null", O_WRONLY);
		/*Close on exec*/
		fcntl(command->inputFile, F_SETFD, FD_CLOEXEC);
		fcntl(command->outputFile, F_SETFD, FD_CLOEXEC);
	}
	return 0;
}

Command* parseCommand(char buffer[BUFF_SIZE]) {
	Command* command = malloc(sizeof(Command));
	int i = 0;
	int argNum = 0;

	/*If comment line return null*/
	if (buffer[0] == '#') return 0;

	/*stdout*/
	command->outputFile = 1;

	/*Tokenizes the buffern into an args array*/
	while (i < BUFF_SIZE && buffer[i] != '\0') {
		/*Splitting on white space*/
		if (buffer[i] == ' ' || buffer[i] == '\t' || buffer[i] == '\n') {
			buffer[i] = '\0';
			/*Filtering out empty strings*/
			if (strcmp(buffer, "")) {
				command->args[argNum++] = buffer;
			}
			buffer = buffer + i + 1;
			i = 0;
		} else i++;
	}

	/*Returns null if no arguments*/
	if (!argNum) return 0;
	
	if (backgroundCheck(command, argNum) < 0) return 0;

	if (redirectionCheck(command, argNum) < 0) return 0;

	return command;
}

Command* getNextCommand() {
	size_t bufferSize = 0;
	char* buffer = 0;
	/*Flush streams for clean read*/
	fflush(stdin);
	getline(&buffer, &bufferSize, stdin);
	return parseCommand(buffer);
}

void exitCommand() {
	/*Ignores SIGHUP*/
	signal(SIGHUP, SIG_IGN);
	/*Sends SIGHUP signal to all related processes*/
	kill(-getpid(), SIGHUP);
	exit(0);
}

void cdCommand(char* path) {
	/*Use a default path if none provided*/
	if (!path) path = getenv("HOME");
	chdir(path);
}

void runCommand(Command* command) {
	int exitStatus;
	int pid = fork();

	/*If child process*/
	if (pid == 0) {
		if(!command->isBackground)
		{
			/*Default disposition*/
			signal(SIGINT, SIG_DFL);
		}
		/*Redirect IO*/
		dup2(command->inputFile, 0);
		dup2(command->outputFile, 1);
		/*If returns error*/
		if (execvp(command->args[0], command->args)) {
			fprintf(stderr, "%s\n", "command could not be executed");
			fflush(stderr);
			sprintf(status, "%s", "exit value 1");
			_exit(1);
		}
	}
	
	if (!command->isBackground) {
		/*Block until child finishes*/
		waitpid(pid, &exitStatus, 0);
		/*If signal recieved*/
		if (WIFSIGNALED(exitStatus)) {
			printf("%s%d\n", "terminated by signal ", WTERMSIG(exitStatus));
			fflush(stdout);
			sprintf(status, "%s%d", "terminated by signal ", WTERMSIG(exitStatus));
		}
		/*If clean exit*/
		if (WIFEXITED(exitStatus)) {
			sprintf(status, "%s%d", "exit value ", WEXITSTATUS(exitStatus));
		}
	} else {
		printf("%s%d\n", "background pid is ", pid);
		fflush(stdout);
	}
}

void cleanUpBackground() {
	int exitStatus;
	/*Get PID of waitable child*/
	int pid = waitpid(-1, &exitStatus, WNOHANG);
	/*If PID was set*/
	if (pid > 0) {
		if (WIFSIGNALED(exitStatus)) {
			printf("%s%d%s%d\n", "background pid ", pid, " is done: terminated by signal ", WTERMSIG(exitStatus));
			fflush(stdout);
		}
		if (WIFEXITED(exitStatus)) {
			printf("%s%d%s%d\n", "background pid ", pid, " is done: exit value ", WEXITSTATUS(exitStatus));
			fflush(stdout);
		}
		/*Recursion while PID is set*/
		cleanUpBackground();
	}
}

int main() {
	Command* command;

	/*Ignore SIGINT*/
	signal(SIGINT, SIG_IGN);
	
	while (1) {
		printf("%s", ": ");
		fflush(stdout);
		command = getNextCommand();
		cleanUpBackground();

		if (!command) {
			continue;
		} else if (!strcmp(command->args[0], "cd")) {
			cdCommand(command->args[1]);
		} else if (!strcmp(command->args[0], "exit")) {
			free(command);
			exitCommand();
		} else if (!strcmp(command->args[0], "status")) {
			puts(status);
			fflush(stdout);
		} else {
			runCommand(command);
		}
	}
	
	return 0;
}
