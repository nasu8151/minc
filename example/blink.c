char [[address = 0x04]] PORTA_OUT;
char [[address = 0x05]] PORTA_DIR;

char [[address = 0x0018]] TIMER8_CONFIG;
char [[address = 0x0019]] TIMER8_COMPARE;
char [[address = 0x001A]] TIMER8_TOP;
char [[address = 0x001B]] TIMER8_COUNTER;
char [[address = 0x001C]] TIMER8_STATUS;

int millis;

void [[isr = 0]] timerinterrupt() {
    TIMER8_STATUS = 0b00000001;
    millis = millis + 1;
}

char main() {
    millis = 0;
    TIMER8_TOP = 211;
    TIMER8_CONFIG = 0b00111011;
    PORTA_DIR = 0xFF;
    while (1) {
        int cur = millis;
        PORTA_OUT = 0x01;
        while ((cur + 500) > millis) {}
        cur = millis;
        PORTA_OUT = 0x00;
        while ((cur + 500) > millis) {}
    }
}
