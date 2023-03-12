#include <malloc.h>
#include <unistd.h>

typedef struct cmd {
    char *name;
    char **argv;
    int argc;
    char *out;
    int rewrite;
}cmd;


void free_cmd(cmd **commands, int cmd_size) {
    for (int i = 0; i < cmd_size; i++) {
        cmd *command = commands[i];
        for (int j = 0; j < command->argc; j++) {
            free(command->argv[j]);
        }
        free(command->argv);
        free(command->out);
        free(command->name);
        free(command);
    }
    free(commands);
}


void append_arg(cmd *command, char* arg) {
    if (command->argc == 0) {
        command->argv = malloc(sizeof(char));
    } else {
        command->argv = realloc(command->argv, sizeof(char) * (command->argc + 1));
    }
    command->argv[command->argc++] = arg;
}

char *get_arg(char* line, int i_start, int i_end) {
    char *arg = calloc(i_end - i_start + 1, sizeof(char));
    int i = 0;
    int line_i = i_start;
    for (;;) {
        if (line_i == i_end) break;
        char c = line[line_i++];

        if (line_i - 1 == i_start && (c == '"' || c == '\'')) continue;
        if (i > 0 && line[line_i - 2] == '\\' && c == '\\') continue;
        if (line_i - 1 == i_end - 1 && ((line[i_start] == '"' || line[i_start] == '\'') && line[i_start] == line[i_end - 1]))
            continue;
        arg[i++] = c;
    }
    arg[i] = '\0';
    return arg;
}

cmd ** parser(char *line, int *command_counter) {
    int cmd_id = 0;
    int c_index = 0;
    int arg_start = 0;
    int is_text = 0;
    char last_text_char = 0;
    int has_name = 0;
    int is_out = 0;
    int is_rewrite = 0;
    cmd **commands = malloc(sizeof(cmd));
    commands[0] = malloc(sizeof(cmd));
    commands[0]->argc = 0;

    for (;;) {
        char current = line[c_index];

        if (current == '>') {
            is_out = 1;
            is_rewrite = 1;
            if (line[++c_index] == '>') {
                is_rewrite = 0;
                c_index++;
            }
            c_index++;
            arg_start = c_index;
            continue;
        }

        if (current == '|') {
            commands = realloc(commands, sizeof(cmd) * (++cmd_id + 1));
            commands[cmd_id] = malloc(sizeof(cmd));
            commands[cmd_id]->argc = 0;
            has_name = 0;
            c_index += 2;
            arg_start = c_index;
            continue;
        }

        int is_com = (c_index > 0 && line[c_index - 1] == '\\') && !(c_index > 1 && line[c_index - 2] == '\\');

        if ((current == '"' || current == '\'') && !is_com) {
            if (is_text) {
                if (last_text_char == current) is_text = 0;
            } else {
                is_text = 1;
                last_text_char = current;
            }
        }

        int is_end = current == '\0';

        if ((!is_text && current == ' ' && !is_com) || is_end) {
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

        if (is_end) break;
        c_index++;
    }
    *command_counter = cmd_id + 1;
    return commands;
}

char *get_line() {
    char* line = calloc(10, sizeof(char));
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
            if (!sup && !is_text) {
                break;
            } else {
                if (sup) line[--i] = ' ';
            }
        } else {
            line[i++] = c;
        }
    }
    line[i] = '\0';
    return line;
}


int main(int argc, char *argv[]) {
    char* line = get_line();
    printf("%s", line);
    int c;
    cmd **commands = parser(line, &c);
    char *args[commands[0]->argc + 1];
    int i = 0;
    for (i; i < commands[0]->argc; i++) {
        args[i] = commands[0]->argv[i];
    }
    args[i] = NULL;
    execvp(commands[0]->name, args);
}