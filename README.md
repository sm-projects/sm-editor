# sm-editor
A text editor from scratch

## Program Design
- Write a main loop that uses read() to respond to input from stdin.
- Put the terminal into "raw" mode - disable echoing, read one keypress at a time, etc. Save and restore the terminal configuration on program exit.
- Add cursor movement.
- Add file I/O and the ability to view files.
- Add scrolling for when the file is bigger than the screen size.
- Add a "rendering" translation layer which case be used to eg. display \t as a fixed number of spaces.
- Add a status bar that shows the filename, current line etc. Also add a message area that can display user messages.
- Add the ability to insert and delete text, with a "dirty" flag that tell the user if the buffer has been modified since last save.
- Add a generic prompt, and then use it to implement incremental search.

Notes

1. [Kilo](http://viewsourcecode.org/snaptoken/kilo/index.html)
2. [Nano](https://www.nano-editor.org/git.php)
3. [Original Kilo](https://github.com/antirez/kilo)
4. [Challenging Projects](https://web.eecs.utk.edu/~azh/blog/challengingprojects.html)
5. [Extending Kilo](https://www.mattduck.com/build-your-own-text-editor.html)
