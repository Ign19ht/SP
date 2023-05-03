#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "string.h"
#include "heap_help.h"

#define WRITE_END 1
#define READ_END 0
#define MY_MODE S_IRWXU | S_IRWXG | S_IRWXO

typedef struct cmd {
	char *name;
	char **argv;
	int argc;
	char *out;
	int rewrite;
} cmd;


void free_cmd(cmd **commands, int cmd_size) {
	for (int i = 0; i < cmd_size; i++) {
		cmd *command = commands[i];
		for (int j = command->argc - 2; j >= 0; j--) {
			free(command->argv[j]);
		}
		free(command->argv);
		if (command->out) free(command->out);
		free(command);
	}
	free(commands);
}


void append_arg(cmd *command, char *arg) {
	if (command->argc == 0) {
		command->argv = malloc(sizeof(char*));
	} else {
		command->argv = realloc(command->argv, sizeof(char*) * (command->argc + 1));
	}
	command->argv[command->argc++] = arg;
}

char *get_arg(char *line, int i_start, int i_end) {
	char *arg = calloc(i_end - i_start + 1, sizeof(char));
	int i = 0;
	int line_i = i_start;
	int is_text = (line[i_start] == '"' || line[i_start] == '\'');
	int counter = 0;
	for (;;) {
		char c = line[line_i++];

		if (line_i - 1 == i_start && is_text) continue;
		if (line_i - 1 == i_end - 1 && is_text) continue;
		if (c == '\\') {
			counter++;
			continue;
		}
		if (counter > 0) {
			if (!is_text) counter--;
			counter = counter / 2 + counter % 2;
			int is_spec = counter % 2;
			counter -= is_spec;
			for (; counter > 0; counter--) arg[i++] = '\\';
			if (is_spec) {
				if (c == 'n') {
					arg[i++] = '\n';
					continue;
				} else arg[i++] = '\\';
			}
		}
		if (line_i - 1 == i_end) break;
		arg[i++] = c;
	}
	arg[i] = '\0';
	return arg;
}

cmd **parser(char *line, int *command_counter, int *has_comm) {
	int cmd_id = 0;
	int c_index = 0;
	int arg_start = 0;
	int is_text = 0;
	char last_text_char = 0;
	int has_name = 0;
	int is_out = 0;
	int is_rewrite = 0;
	int counter = 0;
	cmd **commands = malloc(sizeof(cmd*));
	commands[0] = malloc(sizeof(cmd));
	commands[0]->argc = 0;
	commands[0]->out = NULL;

	for (;;) {
		char current = line[c_index];

		if (current == '\\') {
			counter++;
			c_index++;
			continue;
		}
		int is_com = counter % 2;
		counter = 0;
		*has_comm =  current == '#' && !is_com && !is_text;
		int is_end = current == '\0' || *has_comm;


		if ((current == '"' || current == '\'') && !is_com) {
			if (is_text) {
				if (last_text_char == current) is_text = 0;
			} else {
				is_text = 1;
				last_text_char = current;
			}
		}

		if ( is_end || (!is_text && ((current == '>' || current == '|' || current == ' ') && !is_com))) {
			if (arg_start < c_index) {
				char *arg = get_arg(line, arg_start, c_index);
				if (is_out) {
					commands[cmd_id]->out = arg;
					commands[cmd_id]->rewrite = is_rewrite;
					is_out = 0;
				} else {
					if (!has_name) {
						commands[cmd_id]->name = arg;
						has_name = 1;
					}
					append_arg(commands[cmd_id], arg);
				}
			}
			arg_start = c_index + 1;
		}

		if (current == '>' && !is_com && !is_text) {
			is_out = 1;
			is_rewrite = 1;
			if (line[c_index + 1] == '>') {
				is_rewrite = 0;
				c_index++;
			}
			arg_start = c_index + 1;
		}

		if (current == '|' && !is_com && !is_text) {
			append_arg(commands[cmd_id], NULL);
			commands = realloc(commands, sizeof(cmd*) * (++cmd_id + 1));
			commands[cmd_id] = malloc(sizeof(cmd));
			commands[cmd_id]->argc = 0;
			commands[cmd_id]->out = NULL;
			has_name = 0;
			arg_start = c_index + 1;
		}

		if (is_end) break;
		c_index++;
	}
	append_arg(commands[cmd_id], NULL);
	*command_counter = cmd_id + 1;
	return commands;
}

char *get_line() {
	char *line = calloc(10, sizeof(char));
	int i = 0;
	int len_max = 10;
	char c;
	int is_text = 0;
	char last_text_char = 0;

	for (;;) {
		c = fgetc(stdin);
		if (c == EOF) break;

		if (i + 1 == len_max) {
			len_max *= 2;
			line = realloc(line, sizeof(char) * len_max);
		}
		int sup = (i > 0 && line[i - 1] == '\\') && !(i > 1 && line[i - 2] == '\\');

		if ((c == '"' || c == '\'') && !sup) {
			if (is_text) {
				if (last_text_char == c) is_text = 0;
			} else {
				is_text = 1;
				last_text_char = c;
			}
		}

		if (c == '\n') {
			if (sup) {
				i--;
			} else if (is_text) {
				line[i++] = c;
			} else {
				break;
			}
		} else {
			line[i++] = c;
		}
	}
	line[i] = '\0';
	return line;
}


void child_work(int child, cmd *command, int *pipe1, int *pipe2) {
	if (child == 0) {
		if (pipe1) {
			close(pipe1[WRITE_END]);
			dup2(pipe1[READ_END], STDIN_FILENO);
		}
		if (pipe2) {
			close(pipe2[READ_END]);
			dup2(pipe2[WRITE_END], STDOUT_FILENO);
		}

		if (command->out) {
			int out;
			if (command->rewrite) {
				out = open(command->out, O_CREAT | O_WRONLY | O_TRUNC, MY_MODE);
			} else {
				out = open(command->out, O_CREAT | O_WRONLY | O_APPEND, MY_MODE);
			}
			dup2(out, STDOUT_FILENO);
			close(out);
		}

		if (strcmp(command->name, "cd") == 0 || strcmp(command->name, "exit") == 0) {
			exit(EXIT_SUCCESS);
		}

		execvp(command->name, command->argv);

		fprintf(stderr, "%s failed.\n", command->name);
		exit(EXIT_FAILURE);
	}
}


int main(int argc, char *argv[]) {
	for (;;) {
//		printf("$> ");
		char *line = get_line();
		int c = 0;
		int has_comm = 0;
		cmd **commands = parser(line, &c, &has_comm);
		free(line);

		if (c == 1 && commands[0]->argc == 1) {
			free_cmd(commands, 1);
			if (has_comm) continue;
			return 0;
		}

		if (c == 1 && strcmp(commands[0]->name, "exit") == 0) {
			int code = 0;
			if (commands[0]->argc > 2) code = atoi(commands[0]->argv[1]);
			free_cmd(commands, c);
			return code;
		}

		if (c > 0 && strcmp(commands[0]->name, "cd") == 0) {
			chdir(commands[0]->argv[1]);
		}
		int fd[c - 1][2];
		for (int i = 0; i < c - 1; i++) {
			pipe(fd[i]);
		}

		int child[c];
		for (int i = 0; i < c; i++) {
			int *pipe1 = NULL;
			int *pipe2 = NULL;
			child[i] = fork();
			if (child[i] == 0) {
				for (int j = 0; j < c - 1; j++) {
					if (j == i - 1) pipe1 = fd[j];
					else if (j == i) pipe2 = fd[j];
					else {
						close(fd[j][READ_END]);
						close(fd[j][WRITE_END]);
					}
				}
			}
			child_work(child[i], commands[i], pipe1, pipe2);
		}

		for (int i = 0; i < c - 1; i++) {
			close(fd[i][READ_END]);
			close(fd[i][WRITE_END]);
		}

		int status;
		for (int i = 0; i < c; i++) {
			waitpid(child[i], &status, 0);
		}
		free_cmd(commands, c);
	}
	return 0;
}