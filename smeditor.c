/**
 *  A simple text editor implementation.
 */

//Add feature test macros
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include<sys/ioctl.h>
#include<sys/types.h>
#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<ctype.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>

#define CTRL_KEY(k)  ((k) & 0x1f)
#define SMEDITOR_VERSION "Alpha-0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT ,
    ARROW_UP ,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY, // Home key could be sent as <esc>[1~, <esc>[7~, <esc>[H, or <esc>OH
    END_KEY, //the End key could be sent as <esc>[4~, <esc>[8~, <esc>[F, or <esc>OF
    DEL_KEY // sends the escape sequence <esc>[3~
};

// Struct to store each row  of text in the editor.
typedef struct erow {
    int size;
    char *chars;
}erow;


// A struct to hold edtor configs and state.
struct editorConfig {
 int cx,cy; //track cursor's position
 //Number of icols and rows in the screen available from ioctl
 int screen_rows;
 int screen_cols;
 int num_rows;
 erow *row; //Make it an array of rows in order to store rows read from a file.
 struct termios orig_termios;
};

struct editorConfig editC;

/** ================= All terminal handling functions. ==========================*/

/**
 * Error handling routine
 * perror() looks at the global erronum and prints out the message
 * provided by the string s
 */
void handleError(const char *s) {
    //Clear screen before exit
    write(STDOUT_FILENO, "\x1b[2J",4); //J command erases everything in display
    write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col

    perror(s);
    exit(1);
}
/**
 *
 *  We save the original terminal attributes in the orig_termios struct and call
 *  this function to set the user's terminal back to original state.
 */
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO,TCSAFLUSH, &editC.orig_termios) == -1){
        handleError("SMEditor: Failed to diable raw mode.");
    }
}

/**
 * Enable arrow navigation keys. Pressing an arrow key sends multiple bytes as input
 * to our program. These bytes are in the form of an escape sequence that starts
 * with '\x1b', '[', followed by an 'A', 'B', 'C', or 'D' depending on which of
 * the four arrow keys was pressed.
 * editorReadKey() to read escape sequences of this form as a single keypress
 * Detect page up and down and multiple codes for HOME and END keys.
 */
int editorReadKey() {
    int nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) handleError("SMEDITOR: Error reading charecter.");
    }
    if (c == '\x1b') {
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if(seq[1] >= '0' && seq[1] <= '9') {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == '0') {
            switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    } else {
       return c;
    }
}

/**
 * The n command (Device Status Report) can be used to query the terminal for status
 * information. We want to give it an argument of 6 to ask for the cursor position.
 * Then we can read the reply from the standard input.
 * -----------------------------------------------------------------------------
 * Let’s print out each character from the standard input to see what the reply
 * looks like.
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i =0;

    if (write(STDOUT_FILENO, "\x1b[6n",4) != 4) return -1;

    while( i < sizeof(buf) - 1 ){
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    //printf("\r\n &buf[i]: '%s'\r\n", &buf[i]);

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
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
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO,"\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/** ======================== All Editor row manipulation functions  . ===================*/

void editorAppendRow(char *s, size_t len) {
    editC.row = realloc(editC.row, sizeof(erow) * (editC.num_rows + 1));

    int at = editC.num_rows;
    editC.row[at].size = len;
    editC.row[at].chars = malloc(len + 1);
    memcpy(editC.row[at].chars, s, len);
    editC.row[at].chars[len] = '\0';
     editC.num_rows++;
}


/** ======================== All write buffer handling goes here. ===================*/
struct appendBuf {
    char *buf;
    int  len;
};

#define BUFFER_INIT  {NULL, 0}

/**
 * This method appends a string s to an abuf, the first thing it does is to make sure
 * there is enough memory to hold the new string.
 * It calls realloc() to give us a block of memory that is the size of the current
 * string plus the size of the string being appended.
 * realloc() will either extend the size of the block of memory we already have
 * allocated, or it will take care of free()ing the current block of memory and
 * allocating a new block of memory somewhere else that is big enough for our new string.
 * Then we use memcpy() to copy the string s after the end of the current data in the
 * buffer, and we update the pointer and length of the abuf to the new values.
 */
void appendToBuffer(struct appendBuf *ab, const char *s, int len) {
    char *new = realloc(ab->buf,ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->buf = new;
    ab->len += len;
}

/**
 * abFree() is a destructor that deallocates the dynamic memory used by an abuf.
 */
void bufferFree(struct appendBuf *ab) {
  free(ab->buf);
}

/** ===================== All keyboard input handling functions. =====================*/

void editorMoveCursor(int key) {
    switch(key) {//the if checks prevent the cursor from going off the screen
        case ARROW_LEFT:
            if (editC.cx != 0) {
                editC.cx--;  //move left
            }
            break;
        case ARROW_RIGHT:
            if (editC.cx != editC.screen_cols - 1) {
                editC.cx++;  //move right
            }
            break;
        case ARROW_UP:
            if(editC.cy != 0) {
                editC.cy--; //move up
            }
            break;
        case ARROW_DOWN:
            if(editC.cy != editC.screen_rows - 1) {
                editC.cy++; //move right
            }
            break;
    }
}

void editorProcessKeypress() {
    int  c = editorReadKey();
    switch(c) {
        case CTRL_KEY('q'):
            //Clear screen before exit
            write(STDOUT_FILENO, "\x1b[2J",4); //J command erases everything in display
            write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col
            exit(0);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = editC.screen_rows;
                while(times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case HOME_KEY:
            editC.cx = 0; //move cursor to start position
            break;
        case END_KEY:
            editC.cx = editC.screen_cols - 1;
            break;
    }
}

/** ====================== Terminal Handling funct1ions ========================== */

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
        handleError("Problem getting terminical co1nfig struct.");

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
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN );
    raw.c_cc[VMIN] = 0; //set control charecter flags for read() to wait on bytes read, return afterwards
    raw.c_cc[VTIME] = 1; //set cc VTIME to max wait time for read() before it returns

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        handleError("Error changing terminal config attributes.");
    }
}


/**  Editor output functions. *******************************************/

void editorDrawRows(struct appendBuf *ab) {
    int i;
    for (i=0; i<editC.screen_rows; i++){
        if (i >= editC.num_rows ) {
            //Display welcome message only of no file to open
            if(editC.num_rows ==0 && i == editC.screen_rows / 3) {
                char welcomeMessage[80];

                int welcomeLen = snprintf(welcomeMessage, sizeof(welcomeMessage),
                        "SM-Editor -- version %s", SMEDITOR_VERSION);

                if(welcomeLen > editC.screen_cols)
                    welcomeLen = editC.screen_cols;

                //Center the welcome message on the screeni
                int padding = (editC.screen_cols - welcomeLen / 2);

                if(padding) {
                    appendToBuffer(ab,"~",1);
                    padding--;
                }
                while(padding--) appendToBuffer(ab," ",1);
                appendToBuffer(ab,welcomeMessage,welcomeLen);
            } else {
                appendToBuffer(ab, "~", 1);
            }
        } else {
            int len = editC.row[i].size;
            if(len > editC.screen_cols) len = editC.screen_cols;
            appendToBuffer(ab,editC.row[i].chars, len);
         }

        appendToBuffer(ab,"\x1b[K",3); //put a <esc>[K sequence at the end of each line we draw
        if (i <  editC.screen_rows - 1) {
            appendToBuffer(ab, "\r\n", 2);
        }
    }
}

/**
 * In order to clear the screen, we are writing an escape sequence to the terminal.
 * Escape sequences always start with an escape character (27) followed by a [ character.
 * Escape sequences instruct the terminal to do various text formatting tasks, such as
 * coloring text, moving the cursor around, and clearing parts of the screen.
 *
 * In editorRefreshScreen(), we first initialize a new abuf called ab, by assigning
 * BUFFER_INIT to it. We then replace each occurrence of write(STDOUT_FILENO, ...)
 * with abAppend(&ab, ...). We also pass ab into editorDrawRows(), so it too can
 * use abAppend(). Lastly, we write() the buffer’s contents out to standard output
 * and free the memory used by the abuf
 */
void editorClearScreen() {
    struct appendBuf ab = BUFFER_INIT;

    appendToBuffer(&ab, "\x1b[?25l",6); //Hides the cursor
    //appendToBuffer(&ab, "\x1b[2J",4); //J command erases everything in display
    appendToBuffer(&ab, "\x1b[H", 3); //Repositions the cursor to the first row and col

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",editC.cy + 1, editC.cx + 1);
    appendToBuffer(&ab,buf,strlen(buf));


    //appendToBuffer(&ab, "\x1b[H", 3); //Repositions the cursor to the first row and col
    appendToBuffer(&ab, "\x1b[?25h",6); //Hides the cursor

    write(STDOUT_FILENO,ab.buf, ab.len);
    bufferFree(&ab);
}

/** =================== All file IO functions for the editor. =====================*/
void editorOpen(char *filename) {
    FILE *fp = fopen(filename,"r");
    if(!fp) handleError("[SMEditor]: Could not open file.");

    char *line = NULL;
    ssize_t lineCap = 0;
    ssize_t lineLen;

    while((lineLen = getline(&line,&lineCap,fp)) != -1) {
        //strip off the newline or carriage return at the end of the line
        //before copying it into our erow
        while(lineLen >0 && (line[lineLen - 1] == '\n' ||
                             line[lineLen - 1] == '\r'))
            lineLen--;
        editorAppendRow(line, lineLen);
    }
    free(line);
    fclose(fp);
}

/** Editor init. */
void initEditor() {
    //Set cursor position to top left corner
    editC.cx = 0;
    editC.cy = 0;
    editC.num_rows = 0;
    editC.row = NULL;

    if (getWindowSize(&editC.screen_rows, &editC.screen_cols) == -1) {
        handleError("Unable to get window size.");
    }
}

/* =============================== SMEditor ==============================*/
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >= 2) {
        editorOpen(argv[1]);
    }
    // read one byte at a time and quit reading when key pressed is 'q'
    while (1) {
        editorClearScreen();
        editorProcessKeypress();
    }
    return 0;
}
