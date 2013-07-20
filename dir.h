#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "stacktrace.h"


// Module-specific global variables
char* current_dir;

int cd(const char* path);
char* ls();
char* ls_path(const char* path);
void pwd();


