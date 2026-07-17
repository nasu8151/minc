char [[address = 0x00]] PORTA_OUT;
char [[address = 0x01]] PORTA_DIR;

void wait_05ms() {
    for (char i = 0; i < 254; i=i+1) {
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
    for (char i = 0;i < 10; i=i+1) {
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

void main() {
    lcd_init();
    lcd_senddat(0xB2);
    lcd_senddat(0xB3);
}