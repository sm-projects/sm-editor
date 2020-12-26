/**
 *  A simple editor implementation.
 */

#include<sys/ioctl.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>
#include<stdio.h>
#include<errno.h>


#define CTRL_KEY(k)  ((k) & 0x1f)

// A struct to hold edtor configs and state.
struct editorConfig {
 //Number of icols and rows in the screen available from ioctl
 int screen_rows;
 int screen_cols;
 struct termios orig_termios;
};

struct editorConfig editC;

/** All terminal handling functions. ************************************/

/**
 *
 *  We save the original terminal attributes in the orig_termios struct and call
 *  this function to set the user's terminal back to original state.
 */
void disableRawMode() {
    tcsetattr(STDIN_FILENO,TCSAFLUSH, &editC.orig_termios);
}

/**
 * Error handling routine
 * perror() looks at the global erronum and prints out the message
 * provided by the string s
 */
void handleError(const char *s) {
    perror(s);
    //Clear screen before exit
    write(STDOUT_FILENO, "\x1b[2J",4); //J command erases everything in display
    write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col
    exit(1);
}

char editorReadKey() {
    int nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) handleError("SMEDITOR: Error reading charecter.");
    }
    return c;
}

/**
 * The n command (Device Status Report) can be used to query the terminal for status
 * information. We want to give it an argument of 6 to ask for the cursor position.
 * Then we can read the reply from the standard input.
 * -----------------------------------------------------------------------------
 * Let’s print out each character from the standard input to see what the reply
 * looks like.
 */
int getCursorPosition() {
    if (write(STDOUT_FILENO, "\x1b[6n",4) != 4) return -1;

    printf("\r\n");
    char c;
    while(read(STDIN_FILENO,&c,1) == 1) {
        if(iscntrl(c)){
            printf("%d\r\n",c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }
    editorReadKey();

    return -1;
}

/**
 * Get the size of the terminal by simply calling ioctl() with the TIOCGWINSZ request.
 * (As far as I can tell, it stands for Terminal IOCtl (which itself stands for
 * Input/Output Control) Get WINdow SiZe.)
 * ioctl(), TIOCGWINSZ, and struct winsize come from <sys/ioctl.h>
 * On success, ioctl() will place the number of columns wide and the
 * number of rows high the terminal is into the given winsize struct.
 * On failure, ioctl() returns -1.
 *
 * If it succeeded, we pass the values back by setting the int references that
 * were passed to the function.i
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

   /* We also check to make sure the values it gave back weren’t 0,
    * because apparently that’s a possible erroneous outcome.
    */
    if( 1 || ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO,"\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition();
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/** All keyboard input handling functions. ******************************/
void processKeypress() {
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            //Clear screen before exit
            write(STDOUT_FILENO, "\x1b[2J",4); //J command erases everything in display
            write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col
            exit(0);
            break;
    }
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

    if (tcgetattr(STDIN_FILENO, &editC.orig_termios) == -1)
        handleError("Problem getting terminical config struct.");

    /**
     * Register disableRawMode as a callback when main exits such that
     * we can leave everything.
     */
    atexit(disableRawMode);

    struct termios  raw  = editC.orig_termios;
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

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        handleError("Error changing terminal config attributes.");
    }
}

/**       Editor output functions. *******************************************/

void editorDrawRows() {
    int i;
    for (i=0; i<editC.screen_rows; i++){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

/**
 * In order to clear the screen, we are writing an escape sequence to the terminal.
 * Escape sequences always start with an escape character (27) followed by a [ character.
 * Escape sequences instruct the terminal to do various text formatting tasks, such as
 * coloring text, moving the cursor around, and clearing parts of the screen.
 **/
void editorClearScreen() {
    write(STDOUT_FILENO, "\x1b[2J",4); //J command erases everything in display
    write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col
}

/** Editor init. */
void initEditor() {
    if (getWindowSize(&editC.screen_rows, &editC.screen_cols) == -1) {
        handleError("Unable to get window size.");
    }
}
int main() {
    enableRawMode();
    initEditor();

    // read one byte at a time and quit reading when key pressed is 'q'
    while (1) {
        editorClearScreen();
        processKeypress();
    }
    return 0;
}
