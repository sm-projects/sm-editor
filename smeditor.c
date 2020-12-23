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
     */
    raw.c_iflag &=  ~(IXON); //turns of Ctrl S and Q
    // There is an ICANON flag that allows us to turn off canonical mode.
    // This means we will finally be reading input byte-by-byte, instead of line-by-line.
    // Tuen off Ctrl C and Z by setting ISIG flag
    // IEXTEN diables Ctrl V
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN );
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    /* Register disableRawMode as a callback when main exits such that we can leave everything
     * as is afer exit.
     */
    atexit(disableRawMode);
}

int main() {
    enableRawMode();

    char c;
    // read one byte at a time and quit reading when key pressed is 'q'
    while (read(STDIN_FILENO, &c, 1)== 1 && c != 'q');
    //print out the kepresses
    if (iscntrl(c)) {
        printf("%d\r\n",c);
    } else {
        printf("[SMEditor]: Control key pressed. %d\n", c);
        printf("%d ('%c')\r\n",c,c);
    }
    return 0;
}
