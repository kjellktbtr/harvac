/plan Implement more commands:

Some commands are already made on the system, but most are not. Keep in mind that we eventually wish to pipe data from one command to another so commands like `ls | grep -i test` can be performed.

These commands can be simplified for this system supporting their core functionality.

Maybe some functionality can be extracted from NCD to the SDK and then re-used in both these tools and NCD?

## Commands to add to the shell
- [x] ls: Lists directory contents.
- [ ] cp: Copies files and directories. (you decide if we support -r for recursive at this stage. not essential since NCD can do it)
- [ ] mv: Moves or renames files.
- [ ] rm: Removes files or directories. (you decide if we support -r for recursive at this stage)
- [ ] mkdir: Creates new directories.
- [ ] rmdir: Removes empty directories.
- [ ] pwd: Prints the current working directory.

## Commands to add to the /BIN folder

### Text processing
- [ ] cat: Concatenates and displays file content. (support multiple files as args if not already)
- [ ] grep: Searches for patterns within files. (can start with very basic without regular expression only match specificed characters with `-i` for case insensitive)
- [ ] head: Outputs the first part of files. (support specifying number of lines)
- [ ] tail: Outputs the last part of files. (support specifying number of lines)
- [ ] less: Pages through text outputs. (similar to typical linux less command)
- [ ] cut: Removes sections from each line of files. (optional, can be done later)
- [ ] sort: Sorts lines of text files. (optional, can be done later)

### System Administration
- [ ] df: Reports file system disk space usage. (probably need syscalls implemented too?)
- [ ] du: Estimates file space usage. (should be fine)
- [ ] free: Displays amount of free and used memory. (like dos mem command, probably need sys calls and kernel changes to know total and free memory?)
- [x] uname: Prints system information. (already implemented)

If you think some commands that I've suggested adding to the shell makes it bloated, we can create them as executables instead. I'm open for a discussion about that.

After doing this, we are quite close to a fairly usable UNIX/DOS hybrid environement.