/**
 *  A simple editor implem,entation.
 */

#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>
#include<stdio.h>

struct termios orig_termios;

/**
 *  We save the original terminal attributes in the orig_termios struct and call
 *  this function to set the user's terminal back to original state.
 */
void disableRawMode() {
    tcsetattr(STDIN_FILENO,TCSAFLUSH, &orig_termios);
}

/**
 * Error handling routine
 * perror() looks at the global erronum and prints out the message
 * provided by the string s
 */
void handleError(const char *s) {
    perror(s);
    exit(1);
}
/**
 *  In order to set terminal attributes,we need to do the following:
 *   1. Call tcgetattr to read the attributes into a struct
 *   2. Modify attributes within the struct
 *   3. Pass the modified attributes to  write the new terminal attributes using tcsetattr func
 *   atexit() comes from <stdlib.h>. We use it to register our disableRawMode() function to be
 *   called automatically when the program exits, whether it exits by returning from main(),
 *   or by calling the exit() function.
 *   Note: there is no simple way of switching from "cooked mode" to "raw mode" apart from manupilating
 *   a number of flags in terminal attributes structure.
 */
void enableRawMode(){
    tcgetattr(STDIN_FILENO, &orig_termios);

    struct termios  raw  = orig_termios;
    /*
     * ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary.
     * We use the bitwise-NOT operator (~) on this value to get 11111111111111111111111111110111.
     * We then bitwise-AND this value with the flags field, which forces the fourth bit in the
     * flags field to become 0, and causes every other bit to retain its current value.
     *  Also convert carrige return to new line by turning on ICRNL flag.
     *  Turn off a few other flags;
     *  BRKINT - allows a break condition to trigger a SIGINT
     *  INPCK - turns off parity checking
     *  ISTRIP
     */
    raw.c_iflag &=  ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON); //turns of Ctrl S and Q

    /** Turn off all output processing by turning off the OPOST flag. */
    raw.c_oflag &= ~(OPOST);
    // There is an ICANON flag that allows us to turn off canonical mode.
    // This means we will finally be reading input byte-by-byte, instead of line-by-line.
    // Tuen off Ctrl C and Z by setting ISIG flag
    // IEXTEN diables Ctrl V
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN );
    raw.c_cc[VMIN] = 0; //set control charecter flags for read() to wait on bytes read, return afterwards
    raw.c_cc[VTIME] = 1; //set cc VTIME to max wait time for read() before it returns

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    /* Register disableRawMode as a callback when main exits such that we can leave everything
     * as is afer exit.
     */
    atexit(disableRawMode);
}

int main() {
    enableRawMode();

    // read one byte at a time and quit reading when key pressed is 'q'
    while (1) {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
        if (iscntrl(c)) {
            printf("%d\r\n",c);
        }   else {
            printf("[SMEditor]: Control key pressed. %d\n", c);
            printf("%d ('%c')\r\n",c,c);
        }
        if (c == 'q') break;
    }
    return 0;
}
