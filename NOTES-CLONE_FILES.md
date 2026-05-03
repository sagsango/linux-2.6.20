 # Link-> https://man7.org/linux/man-pages/man2/clone.2.html
 CLONE_FILES (since Linux 2.0)
              If CLONE_FILES is set, the calling process and the child
              process share the same file descriptor table.  Any file
              descriptor created by the calling process or by the child
              process is also valid in the other process.  Similarly, if
              one of the processes closes a file descriptor, or changes
              its associated flags (using the fcntl(2) F_SETFD
              operation), the other process is also affected.  If a
              process sharing a file descriptor table calls execve(2),
              its file descriptor table is duplicated (unshared).

              If CLONE_FILES is not set, the child process inherits a
              copy of all file descriptors opened in the calling process
              at the time of the clone call.  Subsequent operations that
              open or close file descriptors, or change file descriptor
              flags, performed by either the calling process or the child
              process do not affect the other process.  Note, however,
              that the duplicated file descriptors in the child refer to
              the same open file descriptions as the corresponding file
              descriptors in the calling process, and thus share file
              offsets and file status flags (see open(2)).
