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
#include<stdarg.h>
#include<ctype.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<time.h>
#include<fcntl.h>

#define CTRL_KEY(k)  ((k) & 0x1f)
#define SMEDITOR_VERSION "Alpha-0.0.1"
#define SMEDITOR_TAB_STOP 8
#define SMEDITOR_QUIT_TIMES 3;

enum editorKey {
    BACKSPACE = 127, //ASCII value
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
    int rsize; //length of the render string
    char *chars;
    char *render; //contains actual charecters to draw on the screen.
}erow;


// A struct to hold edtor configs and state.
struct editorConfig {
 int cx,cy; //track cursor's position
 //Number of icols and rows in the screen available from ioctl
 int screen_rows;
 int screen_cols;
 int num_rows;
 int rowoffset; //keep track of what row of the file,the user has scrolled to
 int coloffset; //keep track of the contents of a row going horizontally
 erow *row; //Make it an array of rows in order to store rows read from a file.
 char *filename; //record the name of the file opened
 char  statusMesg[80];
 time_t status_time;
 int dirtyFlag; //Keep track of any changes made to file since retrieved from disk
 struct termios orig_termios;
};

struct editorConfig editC;

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);

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
        if (nread == -1 && errno != EAGAIN)
            handleError("SMEDITOR: Error reading charecter.");
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
/**
 * This function uses the chars string of an erow to fill in the contents
 * of the render string
 *
 */
void editorUpdateRow(erow *row) {

    int tabs = 0;
    int j;

    //Count charecters  chars in order to allocate memeory for chars
    for(j=0; j < row->size; j++){
        if (row->chars[j] == '\t') tabs++;
    }
    free(row->render);
    row->render = malloc(row->size + tabs*(SMEDITOR_TAB_STOP - 1) + 1);

    int idx = 0;

    for(j=0;j<row->size;j++){
        //if tab encountered fill up render with spaces instead
        if(row->chars[j] == '\t') {
          row->render[idx++] = ' ';
          while (idx % SMEDITOR_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    editC.row = realloc(editC.row, sizeof(erow) * (editC.num_rows + 1));

    int at = editC.num_rows;
    editC.row[at].size = len;
    editC.row[at].chars = malloc(len + 1);
    memcpy(editC.row[at].chars, s, len);
    editC.row[at].chars[len] = '\0';

    editC.row[at].rsize = 0;
    editC.row[at].render = NULL;
    editorUpdateRow(&editC.row[at]);

    editC.num_rows++;
    editC.dirtyFlag++;
}

/**
 * Allows the user to edit the opened file, one char at a time.
 */
void editorInsertCharAt(erow *row, int at, int c) {
    //validate at, which is the index we want to insert the character into
    //at allowed to go past the end of the string in order to insert at the end
    //of the row
    if (at < 0 || at > row->size) at = row->size;
    // allocate one more byte for the chars of the erow
    // (we add 2 because we also have to make room for the null byte),
    // and use memmove() to make room for the new character.
    row->chars = realloc(row->chars, row->size + 2); // Add 2 to make room for null byte.
    //use memmove to make room for the new char.
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    editC.dirtyFlag++;
}

/** i==================================== editor operations. ===================
 *  contains functions that we?ll call from editorProcessKeypress() when we?re
 *  mapping keypresses to various text editing operations
 */
void editorInsertChar(int c) {
    //If editC.cy == editC.numrows, then the cursor is on the tilde line after
    //the end of the file, so we need to append a new row
    if (editC.cy == editC.num_rows) {
        editorAppendRow("",0);
    }
    editorInsertCharAt(&editC.row[editC.cy], editC.cx, c);
    editC.cx++;
}
/** ======================== File IO functions ===================================== */

/**
 *  converts our array of erow structs into a single string that is ready
 *  to be written out to a file.
 */
char * editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    //First we add up the lengths of each row of text, adding 1 to each one
    //for the newline character we?ll add to the end of each line.
    for(j=0;j<editC.num_rows;j++) {
        totlen += editC.row[j].size + 1;
    }
    //Save the total length into buflen, to tell the caller how long the string is
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;

    for(j=0; j<editC.num_rows;j++) {
        memcpy(p, editC.row[j].chars,editC.row[j].size);
        p += editC.row[j].size;
        *p = '\n';
        p++;
    }
    //return buf, expecting the caller to free() the memory
    return buf;
}
void editorOpen(char *filename) {
    free(editC.filename);
    //Allocate memory and make a copy of the given filename using string function strdup
    editC.filename = strdup(filename);
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
    editC.dirtyFlag = 0;
}
/**
 * Note: The normal way to overwrite a file is to pass the O_TRUNC flag to open(),
 * which truncates the file completely, making it an empty file, before writing the
 * new data into it.
 * By truncating the file ourselves to the same length as the data we are planning to write
 * into it, we are making the whole overwriting operation a little bit safer in case the
 * ftruncate() call succeeds but the write() call fails. In that case, the file would
 * still contain most of the data it had before. But if the file was truncated completely
 * by the open() call and then the write() failed, you'd  end up with all of your data lost
 *
 * More advanced editors will write to a new, temporary file, and then rename that file
 * to the actual file the user wants to overwrite, and they?ll carefully check for errors
 * through the whole process.
 *
 */
void editorSave() {
    if (editC.filename == NULL) return;

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(editC.filename, O_RDWR | O_CREAT, 0644);
    if(fd != -1) {
        if(ftruncate(fd,len) != -1) {//sets the file size to the specified length.
            write(fd,buf,len);
            close(fd);
            free(buf);
            editC.dirtyFlag = 0;
            editorSetStatusMsg("%d bytes written to disk.", len);
            return;
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMsg("Failed to save file to disk: %s", strerror(errno));
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
    // check if the cursor is on an actual line. If it is, then the row
    // variable will point to the erow that the cursor is on, and we?ll
    // check whether E.cx is to the left of the end of that line before we
    // allow the cursor to move to the right.
    erow *row = (editC.cy >= editC.num_rows) ? NULL : &editC.row[editC.cy];

    switch(key) {//the if checks prevent the cursor from going off the screen
        case ARROW_LEFT:
            if (editC.cx != 0) {
                editC.cx--;  //move left
            } else if (editC.cy > 0){
                //allow user to move to the end of  the previous line
                editC.cy--;
                editC.cx = editC.row[editC.cy].size;
            }
            break;
        case ARROW_RIGHT:
            //Allow moving right at the end of a line
            if (row  && editC.cx < row->size) {
                editC.cx++;  //move right
            }else if(row && editC.cx == row->size) {
               editC.cy++;
                editC.cx = 0;
            }
            break;
        case ARROW_UP:
            if(editC.cy != 0) {
                editC.cy--; //move up
            }
            break;
        case ARROW_DOWN:
            if(editC.cy <  editC.num_rows) {
                editC.cy++; //move right
            }
            break;
    }
    // set row again, since E.cy could point to a different line than it did before.
    // We then set E.cx to the end of that line if E.cx is to the right of the end
    // of that line. Also note that we consider a NULL line to be of length 0,
    row = (editC.cy >= editC.num_rows) ? NULL : &editC.row[editC.cy];
    int rowlen = row ? row->size : 0;
    if(editC.cx > rowlen){
        editC.cx = rowlen;
    }
}

/**
 * Main handler for all key press events
 *
 */
void editorProcessKeypress() {

    static int quitTimes = SMEDITOR_QUIT_TIMES;
    int  c = editorReadKey();
    switch(c) {
        case CTRL_KEY('q'):
            if (editC.dirtyFlag && quitTimes > 0) {
                editorSetStatusMsg("WARNING!! File has unsaved changes. "
                        "Press Ctrl-q %d more times to quit.", quitTimes);
                quitTimes--;
                return;
            }
            //Clear screen before exit
            write(STDOUT_FILENO, "\x1b[2J",4); //J command erases everything in display
            write(STDOUT_FILENO, "\x1b[H", 3); //Repositions the cursor to the first row and col
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case '\r':
            //TODO
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
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            /** TODO */
        case CTRL_KEY('l'):
        case '\x1b':
            break;
        default:
            editorInsertChar(c);
            break;
    }
    quitTimes = SMEDITOR_QUIT_TIMES;
}

/**  Editor output functions. *******************************************/
void editorScroll() {
    if (editC.cy < editC.rowoffset) {
        editC.rowoffset = editC.cy;
    }
    if (editC.cy >= editC.rowoffset + editC.screen_rows){
        editC.rowoffset = editC.cy - editC.screen_rows + 1;
    }
    if (editC.cx <  editC.coloffset ){
        editC.coloffset = editC.cx;
    }
    if (editC.cx >= editC.coloffset + editC.screen_cols){
        editC.coloffset = editC.cx - editC.screen_cols + 1;
    }
}

void editorDrawRows(struct appendBuf *ab) {
    int i;
    for (i=0; i<editC.screen_rows; i++){
        int filerow = i + editC.rowoffset;
        if (filerow  >= editC.num_rows ) {
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
            int len = editC.row[filerow].rsize - editC.coloffset;
            if (len < 0) len =0; //prevents len from being negative
            if(len > editC.screen_cols) len = editC.screen_cols;
            appendToBuffer(ab,&editC.row[filerow].render[editC.coloffset], len);
         }
        //put a <esc>[K sequence at the end of each line we draw
        appendToBuffer(ab,"\x1b[K",3);
        //Draw a status bar
        //if (i <  editC.screen_rows - 1) {
        appendToBuffer(ab, "\r\n", 2);
        //}
    }
}

/**
 * Draw a status bar at the bottom of the screen editor
 */
void editorDrawStatusbar(struct appendBuf *ab) {
    //Use the 'm' command to causes the text printed after it to be
    //printed with various possible attributes including
    //bold (1), underscore (4), blink (5), and inverted colors (7).
    //E.g. specify all of these attributes using the command <esc>[1;4;5;7m.
    //An argument of 0 clears all attributes, and is the default argument,
    //so we use <esc>[m to go back to normal text formatting.
    appendToBuffer(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];

    int len = snprintf(status,sizeof(status), "%.20s - %d lines %s",
                        editC.filename ? editC.filename :  "[NO NAME]", editC.num_rows,
                        editC.dirtyFlag ? "(file modified)" : "Unchanged");
    //show the current line number
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", editC.cy + 1, editC.num_rows);
    if(len > editC.screen_cols) len = editC.screen_cols;
    appendToBuffer(ab, status, len);

    while(len < editC.screen_cols) {
        if(editC.screen_cols - len == rlen) {
            appendToBuffer(ab, rstatus, rlen);
            break;
        } else {
            appendToBuffer(ab, " ",1);
            len++;
        }
    }
    appendToBuffer(ab, "\x1b[m ", 3);
    appendToBuffer(ab, "\r\n ", 2);
}

/**
 * Shows help and editor prompt messages to the users
 */
void editorSetStatusMsg(const char *fmt, ...) {
    va_list ap;

    va_start(ap,fmt);
    vsnprintf(editC.statusMesg, sizeof(editC.statusMesg), fmt, ap);
    va_end(ap);
    editC.status_time = time(NULL);
}
/**
 *Draws status bar message.
 *
 */
void editorDrawMsgBar(struct appendBuf *ab) {
    //Clear the message bar
    appendToBuffer(ab, "\x1b[K", 3);
    int len =  strlen(editC.statusMesg);
    if (len > editC.screen_cols) len = editC.screen_cols;
    if (len && (time(NULL) - editC.status_time) < 5)
        appendToBuffer(ab, editC.statusMesg, len);
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
void editorRefreshScreen() {
    editorScroll();
    struct appendBuf ab = BUFFER_INIT;

    appendToBuffer(&ab, "\x1b[?25l",6); //Hides the cursor
    //appendToBuffer(&ab, "\x1b[2J",4); //J command erases everything in display
    appendToBuffer(&ab, "\x1b[H", 3); //Repositions the cursor to the first row and col

    editorDrawRows(&ab);
    editorDrawStatusbar(&ab);
    editorDrawMsgBar(&ab);

    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",(editC.cy - editC.rowoffset) + 1,
                                           (editC.cx - editC.coloffset) + 1);
    appendToBuffer(&ab,buf,strlen(buf));


    //appendToBuffer(&ab, "\x1b[H", 3); //Repositions the cursor to the first row and col
    appendToBuffer(&ab, "\x1b[?25h",6); //Hides the cursor

    write(STDOUT_FILENO,ab.buf, ab.len);
    bufferFree(&ab);
}

/** Editor init. */
void initEditor() {
    //Set cursor position to top left corner
    editC.cx = 0;
    editC.cy = 0;
    editC.num_rows = 0;
    editC.row = NULL;
    editC.rowoffset = 0;
    editC.coloffset = 0;
    editC.filename = NULL;
    editC.statusMesg[0] = '\0';
    editC.status_time = 0;
    editC.dirtyFlag = 0;


    if (getWindowSize(&editC.screen_rows, &editC.screen_cols) == -1) {
        handleError("Unable to get window size.");
    }
    //decrement number of rows available to editor in order to make room for status bar
    //make room for status message as well so we need 2 rows
    editC.screen_rows -= 2;
}

/* =============================== SMEditor ==============================*/
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMsg("HELP: Ctrl-S = save | Ctrl-Q = quit");
    // read one byte at a time and quit reading when key pressed is 'q'
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
