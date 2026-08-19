char [[address = 0x04]] PORTA_OUT;
char [[address = 0x05]] PORTA_DIR;

char [[address = 0x0018]] TIMER8_CONFIG;
char [[address = 0x0019]] TIMER8_COMPARE;
char [[address = 0x001A]] TIMER8_TOP;
char [[address = 0x001B]] TIMER8_COUNTER;
char [[address = 0x001C]] TIMER8_STATUS;

int millis_count;

void [[isr = 0]] timerinterrupt() {
    TIMER8_STATUS = 0b00000001;
    millis_count = millis_count + 1;
}

int millis() {
    cli();
    int m = millis_count;
    sei();
    return m;
}

char main() {
    millis_count = 0;
    TIMER8_TOP = 211;
    TIMER8_CONFIG = 0b00111011;
    PORTA_DIR = 0xFF;
    sei();
    int i = 500;
    int previousMillis = millis();
    char state = 0;
    while (1) {
        if ((millis() - previousMillis) > 500) {
            state = !state;
            previousMillis = millis();
        }
        if (state) {
            PORTA_OUT = 0x01;
        } else {
            PORTA_OUT = 0x10;
        }
        for (char i=0;i<50;i=i+1) {}
    }
    PORTA_OUT = 0xFF;
}
