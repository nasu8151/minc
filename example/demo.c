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

void uart_getch() {
    char c;
    while (UARTC_LSR & 0x01) {
        c = UARTC_DATA;
    }
    UARTC_LSR = 0b1110;
    return c;
}

void uart_putch(char c) {
    while ((UARTC_LSR & 0b00100000) ^ 0b00100000) { }
    UARTC_DATA = c;
}

void wait_05ms() {
    for (int i = 0; i < 254; i=i+1) {
    }
}

void lcd_send4(char rs, char c) {
    PORTA_DIR = 0b11110011;
    char v = rs | c;
    PORTA_OUT = v;
    PORTA_OUT = 0b10 | v;
    PORTA_OUT = v;
    PORTA_DIR = 0;
}

void lcd_senddat(char c) {
    lcd_send4(1, c & 0xF0);
    lcd_send4(1, c * 16);
    wait_05ms();
}

void lcd_sendcmd(char c) {
    lcd_send4(0, c & 0xF0);
    lcd_send4(0, c * 16);
    wait_05ms();
}

void lcd_init() {
    lcd_send4(0, 0x30);
    for (int i = 0;i < 10; i=i+1) {
        wait_05ms();
    }
    lcd_send4(0, 0x30);
    wait_05ms();
    lcd_send4(0, 0x30);
    wait_05ms();
    lcd_send4(0, 0x20);
    wait_05ms();
    lcd_sendcmd(0x28);
    lcd_sendcmd(0x08);
    lcd_sendcmd(0x01);
    wait_05ms();wait_05ms();
    lcd_sendcmd(0x06);
    lcd_sendcmd(0x0C);
}

int bs(int idx) {
    int i = idx - 1;
    lcd_sendcmd(0b10000000 | (i));
    lcd_senddat(0x20);
    lcd_sendcmd(0b10000000 | (i));
    uart_putch(0x20);
    uart_putch(0x08);
    return i;
}

void main() {
    uart_init();
    lcd_init();

    int idx = 0;
    while (1) {
        if (UARTC_LSR & 0x01) {
            char c = UARTC_DATA;
            uart_putch(c);
            if (c == 0x08) {
                idx = bs(idx);
            } else {
                lcd_sendcmd(0b10000000 | idx);
                lcd_senddat(c);
                idx = idx + 1;
            }
        }
        if (idx > 15) {
            idx = 0;
        }
    }
}
