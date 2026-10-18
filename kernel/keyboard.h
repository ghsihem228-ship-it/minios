#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);

/* Blocks (sleeping the CPU) until a key has been typed, then returns it. */
char keyboard_getchar(void);

#endif
