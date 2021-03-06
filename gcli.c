#include "gcli.h"

// Some global variables for our command input, and the current directory/command
FILE* input;
char* current_dir;
char* current_cmd;
char working_dir[1024];

int state_paused = 0;

// Track backgrounded processes
pid_t children[_MAX_CHILDREN];
int max_children = _MAX_CHILDREN;
int cur_children = 0;

// BUILTIN: try to change dir to argument
// http://pubs.opengroup.org/onlinepubs/9699919799/functions/chdir.html#tag_16_57_06_01
int cd(const char* path) {
    int ret = 0;

   if (strlen(path) <= 0) {
        _echo(current_dir); // Per instructions
        return 0;
   }

    ret = chdir(path);
    // If success, change current_dir to where we really are
    if (!ret)
        current_dir = getcwd(NULL, 1024);
    // If failed, print well-formed error with prefix as the actual 
    // command for reference like a real shell does
    if (ret)
        perror(current_cmd);
    return ret;
}

// BUILTIN: List directory supplied as argument
// http://pubs.opengroup.org/onlinepubs/009695399/functions/opendir.html
// http://pubs.opengroup.org/onlinepubs/009695399/basedefs/dirent.h.html
int ls_path(const char* path) {
    int ret = 0;
    DIR* dir;
    struct dirent* dir_ent;
    dir = opendir(path);
    if (dir != NULL) {    
        while ( (dir_ent = readdir(dir)) != NULL) {
            printf("%s\n", dir_ent->d_name);
        }
    }
    else {
        perror(current_cmd);
        ret = 1;
    }
    return ret;
}

// BUILTIN: list directory (current)
int ls() {
    return ls_path("./");
}

// BUILTIN: Simply print the current directory
int pwd() {
    printf("%s\n", current_dir );
    return 0;
}

// BUILTIN: Just echo back the argument
int _echo(const char *buf) {
    printf("%s\n", buf);
    return 0;
}

// BUILTIN: Print some basic instructions
// XXX Maybe ghetto-fy it with some ANSI color?!

int help() {
    char buf[1024] = "";
    printf("%s (%d.%d)\n", _GHETTO_NAME_, _GHETTO_VER_MAJOR_, _GHETTO_VER_MINOR_);
    snprintf(buf, 1024, "more %s/help.txt", working_dir);
    exec(buf);
    return 0;
}

// BUILTIN: Try to clear the screen...
int clr() {
    // use standard ANSI-style codes
    write(1,"\E[H\E[2J",7); 
    return 0;
}


// BUILTIN: Show the currently running child processes
int show_children() {
    int i;
    printf("Children currently executing: ");
    for (i = 0; i < sizeof(children)/sizeof(children[0]); i++) {
        printf("%d ", children[i]);
    }
    printf("\n");
    return 0;
}

// BUILTIN: Pause the shell until STDIN presses ENTER.
// Also, SIGSTOP any backgrounded children and SIGCONT when we
// are ready.
int pause() {
    int i;
    int num_paused = 0;
    state_paused = 1;
    for (i=0; i < max_children; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGSTOP);
            num_paused++;
        }   
    }
    char input;
    printf("--PAUSED %d PROCESSES--\nPress enter to continue.", num_paused);
    while(1) {
        input = getc(stdin);
        if (input == '\n' || input == '^')
            break;
        // break if state_paused is changed, e.g. by quit()
        if (state_paused == 0)
            break;
    }
    state_paused = 0;
    for (i=0; i < max_children; i++) {
        if (children[i] > 0) kill(children[i], SIGCONT);
    }
    return 0;
}

// BUILTIN: Quit the shell, also try to gracefully shutdown.
void quit(int code) {
    printf("\nQuitting\n");
    show_children();
    int i;
    for (i=0; i < max_children; i++) 
       if (children[i] > 0) waitpid(children[i], NULL, 0);
    exit(code);
}

// Attempt to EXECUTE a program. Accepts executable files by 
// absolute path or will search through the PATH environment 
// variable. Default execution is foreground.
int exec(char *tmp) {
    pid_t child;
    char args[1024] = "", *ptr;
    char cmd[1024], cmdname[1024], path[1024];
    char *filesep = NULL, *paths = NULL;
    int ret, is_background = 0, i;
    struct stat statbuf;

    strncpy(cmd, tmp, 1024);
    strncpy(path, cmd, 1024);

    // determine if background or foreground
    if (cmd[strlen(cmd)-1] == '&') {
        is_background = 1;
        cmd[strlen(cmd)-1] = 0;
    }

    // Before we exec, check for zombies! 
    if (cur_children > 0) {
        for (i=0; i < max_children; i++) {
            if (waitpid(children[i], NULL, WNOHANG) > 0) {
                cur_children--;
                children[i] = 0;
            }
        }
    }

    if (cur_children >= max_children) {
        printf("Sorry, too many children were spawned.\n");
        return 0;
    }

    child = fork();

    if (child == 0) {
        // Locate initial Path
        ptr = path;
        do {
	    if (*ptr == ' ' && filesep == NULL) break; // space but no / means any path is for an argument!
            if (*ptr == '/') filesep = ptr+1; 
            ptr++;
        } while (ptr != NULL && *ptr != 0);
        if (filesep != NULL) *filesep = 0;
        else filesep = path;

        // Starting from cmdname, find args after space
        ptr = &cmd[filesep-path];
        do {
            if (ptr != NULL && *ptr == ' ') {
                *ptr = 0;
                strncpy(args, ++ptr, 1024);
                break;
            }
            ptr++;
        } while (ptr != NULL && *ptr != 0);
    
        strncpy(cmdname, &cmd[filesep-path], 1024);

        // or use execlp() XXX
        // if user provided no path, try to search
        paths = getenv("PATH");
        if (paths != NULL && strlen(path) == strlen(tmp)) {
            ptr = strtok(paths, ":");
            do {
                snprintf(path, 1024, "%s/%s", ptr, cmdname);
                if (stat(path, &statbuf) == 0) {
                    strncpy(cmd, path, 1024);
                    break;
                }
            } while ((ptr = strtok(NULL, ":")) != NULL);
        }

        if (strlen(args) <= 0) 
            ret = execl( cmd, cmdname, NULL );
        else { 
            trim(args);
            ret = execl( cmd, cmdname, args, NULL );
        }

        if (ret < 0) perror(current_cmd);
        exit(ret);
    }
    else if (child < 0) return -1; // Failed?
    else if (is_background == 0) wait(NULL); 
     else if (is_background == 1) {
        // Find empty slot in array to save child PID
        for (i=0; i < max_children; i++) 
            if (children[i] <= 0) break;

        if (i < max_children) children[i] = child;
        cur_children++;
     }

      return 0;
}

// Given a formatted string, replace special variables with their values
// %uid = $ if non-root, # if root
// %pwd = current directory
char *make_prompt(char *prompt, const char *format) {
    char temp[1024] = "";
    char *ptr;

    strncpy(prompt, format, 1024);

    if ((ptr=strstr(prompt, "%%uid")) != NULL) {
        strncpy(temp, ptr+5, 1024); 
        snprintf(ptr, 1024 - (prompt-ptr), "%s%s", (getuid() == 0) ? "#" : "$", temp);
    }

    if ((ptr=strstr(prompt, "%%pwd")) != NULL) {
        strncpy(temp, ptr+5, 1024);
        snprintf(ptr, 1024 - (prompt-ptr), " %s%s", current_dir, temp);
    }

    return prompt;
}

// This is basically STRNCMP() except instead of returning the match,
// we return a pointer to the REMAINDER of the string (arguments) or NULL
char *test_cmd(char *buf, const char *cmd) {
    int c1, c2, flag;

    if (buf == NULL || cmd == NULL) return NULL;

    // Iterate through each string and check if characters match 
    do {
        c1 = *buf++;
        c2 = *cmd++;
        flag = (c1 == c2);
        // Try to be case-insensitive
        if (!flag) flag = (c1+32 == c2);
        if (!flag) flag = (c1 == c2+32);
    } while ( flag && c1 != 0 && c2 != 0);

    // if we reached the end of CMD, but not BUF, then we matched so return the arguments left in BUF
    if (c2 == 0 && c1 != 0) return buf;

    return NULL;
}

// Prevent ctl-c from terminating.
// Very confusing things happen when you run python and hit ctl-c;
// python continues to run (and jumps to 100% cpu), while the shell exits.
void ctl_c_handler(int s) {
    printf("\nUse ctl-d to quit. Press enter to continue.\n");
}

// Force children to terminate as well during SIGQUIT
void ctl_d_handler(int s) {
    int i;
    // Since we are leaving in a hurry, force stop children w/ SIGTERM
    for (i=0; i < max_children; i++) {
        if (children[i] > 0) {
            // resume a paused child so that it can be terminated correctly
            if (state_paused)kill(children[i], SIGCONT); 
            kill(children[i], SIGTERM);
        }
    }
    quit(0);
}

// trim leading/trailing whitespace
// http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
void trim(char * s) {
    char * p = s;
    int l = strlen(p);
    while(isspace(p[l - 1])) p[--l] = 0;
    while(* p && isspace(* p)) ++p, --l;
    memmove(s, p, l + 1);
}

// Main GHETTO entry point :-)
int main(int argc, char *argv[]) {

    // to debug segmentation faults
    activate_segmentation_handler();

    const char format[1024] = "\x1b[0;1mgcli\x1b[33m\\\x1b[32;1m%%uid\x1b[33;1m/\x1b[0;1m%%pwd\x1b[33;1m>\x1b[00m ";

    char buf[1024] = "";
    char *ptr;
    char history[5][1024];
    int max_history = 5, cur_history = 0, ptr_history = 0;

    // make ctl_d call quit so children terminate as well
    struct sigaction ctl_d;
    ctl_d.sa_handler = ctl_d_handler;
    sigemptyset(&ctl_d.sa_mask);
    ctl_d.sa_flags = 0;
    sigaction(SIGQUIT, &ctl_d, NULL);

    // make ctl_c do nothing    
    struct sigaction ctl_c;
    ctl_c.sa_handler = ctl_c_handler;
    sigemptyset(&ctl_c.sa_mask);
    ctl_c.sa_flags = SA_RESTART;
    sigaction(SIGINT, &ctl_c, NULL);

    current_dir = getcwd(NULL, 1024);
    strncpy( working_dir, current_dir, 1024 );
    input = stdin; // Until getopt tells us differently

    int opt;
    while ((opt = getopt(argc, argv, "vf:")) != -1) {
        switch (opt) {
        case 'v':
            printf("%s: %d.%d\n", _GHETTO_NAME_, _GHETTO_VER_MAJOR_, _GHETTO_VER_MINOR_);
            exit(0);
            break;
        case 'f':
            input = fopen(optarg, "r+");
            if (input == NULL) perror("fopen");
            break;
        default:
            fprintf(stderr, "Usage: %s [-v] [-f FILENAME]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Treat ONE extra argument as a batchfile, else Usage()
    if (argc == 2 && argv[1][0] != '-') {
        input = fopen(argv[1], "r+");
        if (input == NULL) {
		perror("fopen");
		exit(EXIT_FAILURE); // or set input back to stdin
	}
    }

    printf("\x1b[31;1mWelcome to ghettocli. A ghetto command line interpreter.\x1b[0m\n");
    char prompt[1024] = "";

    // main I/O loop 
    do {
        buf[0] = 0; // clear previous line

        // If we have a user, make it pretty
        if (input == stdin) {
            printf("%s", make_prompt(prompt, format));
        }

        // fgets() failed, usually because of SIGQUIT
        fgets(buf, 1024, input);
        if (buf == NULL || strlen(buf) <= 0) {
            if (input == stdin) ctl_d_handler(1);
            break;
        }
        trim(buf);

        // kill trailing \n
        if (buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = 0; 

        // We aint got no time for "Empty lines"
        if (strlen(buf) <= 0) continue;

        // Save this command in the global buffer in case we want to bitch out the user later :P
        current_cmd = buf;

        // And also save it in their history using a 2-dimensional char[] and the wonder of modulus
        strncpy(history[(cur_history++) % max_history], buf, 1024);
        ptr_history = cur_history; // Make sure we point to the latest cmd

        // Now we check for valid commands:
        // Either it matches completely (case-insensitive)
        // OR test_cmd() matches the COMMAND and returns a pointer to the arguments
        if (strncasecmp(buf, "quit", 4) == 0) quit(0);
        else if (strncasecmp(buf, "pwd", 3) == 0) pwd();
        else if (strcasecmp(buf, "ls") == 0) ls(current_dir);
        else if (strcasecmp(buf, "dir") == 0) ls(current_dir);
	else if (strcasecmp(buf, "cd") == 0) cd("");
        else if (strncasecmp(buf, "cls", 3) == 0) clr();
        else if (strncasecmp(buf, "clr", 3) == 0) clr();
        else if (strncasecmp(buf, "clear", 5) == 0) clr();
        else if (strncasecmp(buf, "pause", 5) == 0) pause();
        else if (strncasecmp(buf, "help", 4) == 0) help();
        else if (strncasecmp(buf, "children", 8) == 0) show_children();
        else if ((ptr=test_cmd(buf, "cd")) != NULL) cd(ptr);
        else if ((ptr=test_cmd(buf, "ls")) != NULL) ls_path(ptr);
        else if ((ptr=test_cmd(buf, "dir")) != NULL) ls_path(ptr);
        else if ((ptr=test_cmd(buf, "echo")) != NULL) _echo(ptr);
        else exec(current_cmd); // Exec is the catch-all

    }
    while (1);

    fclose(input);
    quit(0);
    return 0;

}
