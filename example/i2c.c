char [[address = 0x0010]] i2c_presc_l;
char [[address = 0x0011]] i2c_presc_h;
int  [[address = 0x0010]] i2c_presc;
char [[address = 0x0012]] i2c_control;
char [[address = 0x0013]] i2c_transmit;
char [[address = 0x0013]] i2c_recieve;
char [[address = 0x0014]] i2c_command;
char [[address = 0x0014]] i2c_status;

char [[address = 0x04]] PORTA_OUT;
char [[address = 0x05]] PORTA_DIR;

void i2c_init() {
    i2c_presc = 54;
    i2c_control = 0b10000000;
}

char i2c_start(char addr, char rw) {
    i2c_transmit = (addr * 2) | rw;
    i2c_command = 0b10000000;
    PORTA_OUT = 0b10000000;
    char timeout = 0;
    while (i2c_status & 0b10000000) {
        timeout = timeout + 1;
    }
    PORTA_OUT = 0;
    return 0;
}

char i2c_write(char data) {
    i2c_transmit = data;
    i2c_command = 0b00010000;
    PORTA_OUT = 0b01100000;
    char timeout = 0;
    while (i2c_status & 0b10000000) {
        timeout = timeout + 1;
    }
    PORTA_OUT = 0;
    return 0;
}

void main() {
    PORTA_DIR = 0xFF;
    i2c_init();
    i2c_start(0x48, 0);
    i2c_command = 0b01000000;
    PORTA_OUT = 0x01;
}