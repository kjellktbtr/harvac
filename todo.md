Re-factor NCD to use POSIX or Harva SDK APIs
In general, see if more code can use POSIX or Harva SDK APIs
cat use arguments or stdin if no arguments. Not hard coded argument
Investigate if there is a way to share common machine code between multiple .COM files, to save hard drive space. Now several C functions are re-used in the same binary. This shouldn't consume too much memory either (not loading in a lot of maybe used functions in memory). Maybe find a way to load noly needed functions to memory, execute .COM and then clean up afterwards?
check if user applications are using syscalls, if so, migrate to API
investigate if other features are missing
grep maybe ignore -i for ignore case?
`cp DOCS TMP` corrupts TMP it seems, maybe turning it in to a file. What is the correct syntax to copy a dir into another dir?
ls support path. User can type `ls /bin` to see content of been. In general support for full paths in multiple commands
After doing `cat > test.txt` then typing some words and ctrl+d to close it I cannot find the file
`ls | sort` doesn't seem to sort
`du *` should show size for all files (and dirs). Dirs can be done recursively
