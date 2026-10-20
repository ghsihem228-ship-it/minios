#ifndef STARTER_SERIAL_H
#define STARTER_SERIAL_H
/* COM1, 115200 baud, 8 data bits, no parity, 1 stop bit, polling only. */
void serial_init(void);
/* Kernel-only: valid NUL-terminated text. Returns 0 on UART timeout. */
int serial_write(const char *text);
#endif
