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
    PORTA_DIR = 0xFF;
    UARTC_LCR = 0x03;
}

void uart_available() {
    return UARTC_LSR & 0x01;
}

void uart_putch(char c) {
    while (1) {
        if (UARTC_LSR & 0b01100000) break;
    }
    UARTC_DATA = c;
    PORTA_OUT = 0x04;
}

void uart_getch() {
    char c;
    while (uart_available()) {
        c = UARTC_DATA;
    }
    UARTC_LSR = 0b1110;
    return c;
}

void main() {
    uart_init();

    while (1) {
        PORTA_OUT = 0x01;
        if (uart_available()) {
            PORTA_OUT = 0x02;
            char c = uart_getch();
            uart_putch(c);
        }
    }
    uart_putch(97);
}
