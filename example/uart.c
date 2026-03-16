char [[address = 0x00]] PORTA_OUT;
char [[address = 0x01]] PORTA_DIR;

char [[address = 0x08]] UARTC_DATA;
char [[address = 0x09]] UARTC_IER;
char [[address = 0x0A]] UARTC_IIR;
char [[address = 0x0B]] UARTC_LCR;
char [[address = 0x0C]] UARTC_MCR;
char [[address = 0x0D]] UARTC_LSR;
char [[address = 0x0E]] UARTC_MSR;

void uart_init() {
    UARTC_LCR = 0x03;
    PORTA_DIR = 0xFF;
}

void uart_putch(char c) {
    while (~(UARTC_LSR & 0b01000000)) { }
    UARTC_DATA = c;
}

void main() {
    uart_init();
    uart_putch(104);
    uart_putch(101);
    uart_putch(108);
    uart_putch(108);
    uart_putch(111);
}
