#ifndef STARTER_CONSOLE_H
#define STARTER_CONSOLE_H
/* Trusted kernel console. VGA text output mirrored by serial_write(). */
void console_init(void);
void console_clear(void);
void console_putc(char);
int console_input(void); /* one printable/control byte, or -1; polling only */
#endif
