char [[address = 0x08]] UARTC_DATA;
char [[address = 0x09]] UARTC_IER;
char [[address = 0x0A]] UARTC_IIR;
char [[address = 0x0B]] UARTC_LCR;
char [[address = 0x0C]] UARTC_MCR;
char [[address = 0x0D]] UARTC_LSR;
char [[address = 0x0E]] UARTC_MSR;

void uart_init() {
    UARTC_LCR = 0x03;
}

void uart_putch(char c) {
    UARTC_DATA = c;
}

void main() {
    uart_init();
    uart_putch(104);
}
