# SSHchecker

The program downloads students' solutions from the server via ssh to a local disk, and then processes the downloaded files. Processing consists in unpacking various types of archive and analyzing whether it contains all the required components. During analysis, errors may occur, the program identifies such cases and records errors. The program is written in C POSIX using signals, processes and mutexes.
