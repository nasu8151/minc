char [[address = 0x00]] PORTA_OUT;
char [[address = 0x01]] PORTA_DIR;

void wait_05ms() {
    for (int i = 0; i < 254; i=i+1) {
    }
}

void lcd_send4(char rs, char c) {
    char v = rs | c;
    PORTA_OUT = 0b10 | v;
    PORTA_OUT = v;
}

void lcd_senddat(char c) {
    lcd_send4(1, c * 16);
    lcd_send4(1, c | 0b1111);
}

void lcd_sendcmd(char c) {
    lcd_send4(0, c * 16);
    lcd_send4(0, c | 0b1111);
    wait_05ms();
}

void lcd_init() {
    for (int j = 0;j < 3; j=j+1){
        lcd_send4(0,0x3);
        for (int i = 0;i < 7; i=i+1;) {
            wait_05ms();
        }
    }
    lcd_send4(0,0x2);
}