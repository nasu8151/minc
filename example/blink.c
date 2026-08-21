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
    int previousMillis = millis();
    int previousMillis2 = millis();
    char state = 0;
    char state2 = 0;
    char porta;
    while (1) {
        int curr = millis();
        if ((curr - previousMillis) > 500) {
            state = !state;
            previousMillis = curr;
        }
        if ((curr - previousMillis2) > 300) {
            state2 = !state2;
            previousMillis2 = curr;
        }
        if (state) {
            porta = porta | 0x10;
        } else {
            porta = porta & 0xEF;
        }
        if (state2) {
            porta = porta | 0x01;
        } else {
            porta = porta & 0xFE;
        }
        PORTA_OUT = porta;
        for (char i=0;i<50;i=i+1) {}
    }
    PORTA_OUT = 0xFF;
}
