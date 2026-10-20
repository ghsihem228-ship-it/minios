#ifndef COMMAND_H
#define COMMAND_H
/* Compacts a NUL-terminated command in place. Errors discard the whole line. */
int command_split(char *line, unsigned capacity, char **argv, unsigned maximum);
#endif
