#include "grfs.h"
#include "io.h"
#include "cache.h"
#include <stdio.h>
#include <string.h>

int now_ino;

#define MAX_BUFFER_SIZE 256

typedef enum {
    NO_ERROR,
    NORMAL_ERROR,
    NOREMAL_WARNING,
    NO_SUCH_TASK,
    NO_SUCH_CMD,
} wrong_tag_t;

char cmd_history[10][MAX_BUFFER_SIZE];
int cmd_history_index = 0;
int _term_history_index_now = 0;

char cwd[MAX_BUFFER_SIZE] = "/";

static wrong_tag_t run_mkfs(int argc, char** argv){
    if(argc > 1){
        printf("  [MKFS]\033[31m The command 'mkfs' does not need any arguments.\033[0m\n");
        return NORMAL_ERROR;
    }
    int ret = do_mkfs();
    if(ret == 1){
        printf("  [MKFS]\033[32m The file system has been created.\033[0m\n");
    }else{
        printf("  [MKFS]\033[31m The file hae already existed.\033[0m\n");
        return NORMAL_ERROR;
    }
    return NO_ERROR;
}

static wrong_tag_t run_statfs(int argc, char** argv){
    if(argc > 1){
        printf("  [STATFS]\033[31m The command 'statfs' does not need any arguments.\033[0m\n");
        return NORMAL_ERROR;
    }
    int ret = do_statfs();
    if(ret == 0){
        printf("  [STATFS]\033[31m No valid file system now!\033[0m\n");
    }
    return NO_ERROR;
}

static wrong_tag_t run_cd(int argc, char** argv){
    if(argc != 2){
        printf("  [CD]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: cd [Directory]\n");
        return NORMAL_ERROR;
    }
    int ret = do_cd(argv[1]);
    if(ret == -1)
        printf("  [CD]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
    else if(ret == 0)
        printf("  [CD]\033[31m No such file or directory.\033[0m\n");
    do_pwd(cwd);
    return NO_ERROR;
}

static wrong_tag_t run_mkdir(int argc, char** argv){
    if(argc != 2){
        printf("  [MKDIR]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: mkdir [Directory]\n");
        return NORMAL_ERROR;
    }
    int ret = do_mkdir(argv[1]);
    if(ret == -1)
        printf("  [MKDIR]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
    else if(ret == 0)
        printf("  [MKDIR]\033[31m No such file or directory.\033[0m\n");
    else if(ret == -2)
        printf("  [MKDIR]\033[31m The directory already exists.\033[0m\n");
    return NO_ERROR;
}

static wrong_tag_t run_rmdir(int argc, char** argv){
    if(argc != 2){
        printf("  [RMDIR]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: rmdir [Directory]\n");
        return NORMAL_ERROR;
    }
    int ret = do_rmdir(argv[1]);
    if(ret == 1)
        printf("  [RMDIR]\033[32m Remove directory successly.\033[0m\n");
    else if(ret == -2){
        printf("  [RMDIR]\033[31m The directory cannot be removed.\033[0m\n");
        return NORMAL_ERROR;
    } else if(ret == -1){
        printf("  [RMDIR]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    } else if(ret == 0){
        printf("  [RMDIR]\033[31m No such directory.\033[0m\n");
        return NORMAL_ERROR;
    }
    return NO_ERROR;
}

static wrong_tag_t run_ls(int argc, char** argv){
    // if(argc > 3){
    //     printf("  [LS]\033[31m Invalid arguments.\033[0m\n");
    //     printf("      Usage: ls [options] [Directory]\n");
    //     printf("  Options:\n");
    //     printf("      -l: Long listing format.\n");
    //     printf("      -a: List all files, including hidden files.\n");
    //     return NORMAL_ERROR;
    // }
    int has_l = 0, has_a = 0;
    int path_index = 0;
    if(argc > 1){
        for(int i = 1; i < argc; i++){
            if(argv[i][0]=='-'){
                for(int j = 1; argv[i][j] != '\0'; j++){
                    if(argv[i][j] == 'l'){
                        has_l = 1;
                    }else if(argv[i][j] == 'a'){
                        has_a = 1;
                    }else{
                        printf("  [LS]\033[31m Invalid option \033[0m'-%c'\n", argv[i][j]);
                        printf("  Options:\n");
                        printf("      -l: Long listing format.\n");
                        printf("      -a: List all files, including hidden files.\n");
                        return NORMAL_ERROR;
                    }
                }
            } else {
                if(path_index == 0)
                    path_index = i;
                else{
                    printf("  [LS]\033[31m Invalid arguments.\033[0m\n");
                    printf("      Usage: ls [options] [Directory]\n");
                    printf("  Options:\n");
                    printf("      -l: Long listing format.\n");
                    printf("      -a: List all files, including hidden files.\n");
                    return NORMAL_ERROR;
                }
            }
        }
    }
    int ls_option = (has_a ? LS_ALL : 0) | (has_l ? LS_LONG : 0);
    int ret = do_ls(path_index == 0 ? NULL : argv[path_index], ls_option);
    if(ret == -1)
        printf("  [LS]\033[31m Invalid path \033[0m'%s'\n", path_index == 0 ? "./" : argv[path_index]);
    else if(ret == 0)
        printf("  [LS]\033[31m No such directory.\033[0m\n");
    return NO_ERROR;
}
    
static wrong_tag_t run_pwd(int argc, char** argv){
    if(argc > 1){
        printf("  [PWD]\033[31m The command 'pwd' does not need any arguments.\033[0m\n");
        return NORMAL_ERROR;
    }
    char path[MAX_BUFFER_SIZE];
    int ret = do_pwd(path);
    if(ret == 0)
        printf("  [PWD]\033[31m No valid file system now!\033[0m\n");
    else
        printf("  [PWD]\033[32m %s\033[0m\n", path);
    return NO_ERROR;
}

static wrong_tag_t run_touch(int argc, char** argv){
    if(argc != 2){
        printf("  [TOUCH]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: touch [File]\n");
        return NORMAL_ERROR;
    }
    int fd = do_open(argv[1], 0);
    if(fd == -1){
        printf("  [TOUCH]\033[31m Failed to touch file \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    }
    do_close(fd);
    return NO_ERROR;
}

static wrong_tag_t run_rmnod(int argc, char** argv){
    if(argc != 2){
        printf("  [RMNOD]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: rmnod [File]\n");
        return NORMAL_ERROR;
    }
    int ret = do_rmnod(argv[1]);
    if(ret == 1)
        printf("  [RMNOD]\033[32m Remove file successly.\033[0m\n");
    else if(ret == -2){
        printf("  [RMNOD]\033[31m Is a directory.\033[0m\n");
        return NORMAL_ERROR;
    } else if(ret == -1){
        printf("  [RMNOD]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    } else if(ret == 0){
        printf("  [RMNOD]\033[31m No such file.\033[0m\n");
        return NORMAL_ERROR;
    }
    return NO_ERROR;
}

static wrong_tag_t run_rm(int argc, char** argv){
    if(argc != 2){
        printf("  [RM]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: rm [File]\n");
        return NORMAL_ERROR;
    }
    int ret = do_rm(argv[1]);
    if(ret == 1)
        printf("  [RM]\033[32m Remove successly.\033[0m\n");
    else if(ret == -2){
        printf("  [RM]\033[31m Cannot remove it.\033[0m\n");
        return NORMAL_ERROR;
    } else if(ret == -1){
        printf("  [RM]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    } else if(ret == 0){
        printf("  [RM]\033[31m No such file or directory.\033[0m\n");
        return NORMAL_ERROR;
    }
    return NO_ERROR;
}

static wrong_tag_t run_ln(int argc, char** argv){
    if(argc != 3){
        printf("  [LN]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: ln [Source] [Target]\n");
        return NORMAL_ERROR;
    }
    int ret = do_ln(argv[1], argv[2]);
    if(ret == 1){
        printf("  [LN]\033[32m Link create successly.\033[0m\n");
    } else if(ret == 0){
        printf("  [LN]\033[31m No such file\033[0m '%s'.\n", argv[1]);
        return NORMAL_ERROR;
    } else if(ret == -1){
        printf("  [LN]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    } else if(ret == -2){
        printf("  [LN]\033[31m No such file\033[0m '%s'.\n", argv[2]);
        return NORMAL_ERROR;
    } else if(ret == -3){
        printf("  [LN]\033[31m Invalid path \033[0m'%s'\n", argv[2]);
        return NORMAL_ERROR;
    } else if(ret == -4){
        printf("  [LN] '%s'\033[31m is a directory.\033[0m\n", argv[1]);
        return NORMAL_ERROR;
    } else if(ret == -5){
        printf("  [LN] '%s'\033[31m is already exists.\033[0m\n", argv[2]);
        return NORMAL_ERROR;
    } else{
        printf("  [LN]\033[31m Failed to create link.\033[0m\n");
        return NORMAL_ERROR;
    }
    return NO_ERROR;
}

static wrong_tag_t run_echo(int argc, char** argv){
    if(argc == 1){
        printf("  [ECHO]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: echo [Message] [>> [File]]\n");
        return NORMAL_ERROR;
    }
    int has_redirect = 0;
    int redirect_to_new_file = 0;
    int redirect_index = 0;
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], ">>") == 0){
            if(i != argc - 2){
                printf("  [ECHO]\033[31m Invalid arguments.\033[0m\n");
                printf("      Usage: echo [Message] [[> | >>] [File]]\n");
                return NORMAL_ERROR;
            }
            has_redirect = 1;
            redirect_to_new_file = 0;
            redirect_index = i;
        } else if(strcmp(argv[i], ">") == 0){
            if(i != argc - 2){
                printf("  [ECHO]\033[31m Invalid arguments.\033[0m\n");
                printf("      Usage: echo [Message] [[> | >>] [File]]\n");
                return NORMAL_ERROR;
            }
            has_redirect = 1;
            redirect_to_new_file = 1;
            redirect_index = i;
        }
    }
    if(has_redirect){
        if(redirect_to_new_file){
            int find = do_find(argv[redirect_index+1]);
            if(find == 2){
                printf("  [ECHO]\033[31m A directory has the same name.\033[0m\n");
                return NORMAL_ERROR;
            } else if(find == 1){
                do_rmnod(argv[redirect_index+1]);
            }
        }
        int fd = do_open(argv[redirect_index+1], O_WRONLY);
        if(fd == -1){
            printf("  [ECHO]\033[31m Failed to open file \033[0m'%s'\n", argv[redirect_index+1]);
            return NORMAL_ERROR;
        }
        do_lseek(fd, 0, SEEK_END);
        char buf[MAX_BUFFER_SIZE];
        int buf_len = 0;
        for(int i = 1; i < redirect_index; i++){
            memcpy(buf + buf_len, argv[i], strlen(argv[i]));
            buf_len += strlen(argv[i]);
            buf[buf_len++] = ' ';
        }
        buf[buf_len-1] = '\n';
        buf[buf_len] = '\0';
        do_write(fd, buf, buf_len);
        do_close(fd);
    } else {
        for(int i = 1; i < argc; i++){
            printf("%s ", argv[i]);
        }
        printf("\n");
    }
}

static wrong_tag_t run_cat(int argc, char** argv){
    if(argc != 2){
        printf("  [CAT]\033[31m Invalid arguments.\033[0m\n");
        printf("      Usage: cat [File]\n");
        return NORMAL_ERROR;
    }
    int find = do_find(argv[1]);
    if(find == 2){
        printf("  [CAT]\033[31m Is a directory.\033[0m\n");
        return NORMAL_ERROR;
    } else if(find == 0){
        printf("  [CAT]\033[31m No such file.\033[0m\n");
        return NORMAL_ERROR;
    } else if(find == -1){
        printf("  [CAT]\033[31m Invalid path \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    }
    int fd = do_open(argv[1], O_RDONLY);
    if(fd == -1){
        printf("  [CAT]\033[31m Failed to open file \033[0m'%s'\n", argv[1]);
        return NORMAL_ERROR;
    }
    char buff[MAX_BUFFER_SIZE+1];
    int len = 1;
    while(len > 0){
        len = do_read(fd, buff, MAX_BUFFER_SIZE);
        buff[len] = '\0';
        printf("%s", buff);
    }
    do_close(fd);
    return NO_ERROR;
}

static char* _strtok_index = NULL;
static char* mystrtok(char* str, char delim){
    if(str != NULL)
        _strtok_index = str;
    else
        str = _strtok_index;
    if(*str == '\0')
        return NULL;
    while(*_strtok_index == delim && *_strtok_index!= '\0')
        _strtok_index++;
    while(*_strtok_index){
        if(*_strtok_index == delim){
            *_strtok_index++ = '\0';
            break;
        }
        _strtok_index++;
    }
    while(*_strtok_index == delim && *_strtok_index!= '\0')
        _strtok_index++;
    return str;
}

static int find_and(int argc, char** argv){
    if(argc <= 0)
        return -1;
    for(int i = 0; i < argc; i++){
        if(strcmp(argv[i], "&&") == 0)
            return i;
    }
    return argc;
}

static void term_add_history(char* cmd, int valid){
    if(valid){
        if(cmd_history_index == 0 || strcmp(cmd_history[cmd_history_index-1], cmd) != 0){
            if(cmd_history_index == 10){
                for(int i = 1; i < 10; i++){
                    strcpy(cmd_history[i-1], cmd_history[i]);
                }
                cmd_history_index--;
            }
            strcpy(cmd_history[cmd_history_index], cmd);
            cmd_history_index++;
        }
    }
    _term_history_index_now = cmd_history_index;
}

static void term_print_history(int key, char* str, int* len){
    if(cmd_history_index == 0)
        return;
    if(key == 72){
        if(_term_history_index_now > 0)
            _term_history_index_now--;
        else
            return;
    }else if(key == 80){
        if(_term_history_index_now < cmd_history_index)
            _term_history_index_now++;
        else
            return;
    }
    char black[] = "";
    char* output;
    if(_term_history_index_now != cmd_history_index){
        output = cmd_history[_term_history_index_now];
    }
    else{
        output = black;
    }
    char back[] = "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b";
    char space[] = "                              ";
    printf("%s%s%s%s", back + 30 - *len, space + 30 - *len, back + 30 - *len, output);
    *len = strlen(output);
    strcpy(str, output);
}

static void term_write()
{
    printf("\033[1;32mGrfs\033[31m@\033[1;32mTest\033[0m:\033[1;34m%s\033[0m > ", cwd);
}

static int term_analyze(char *str)
{
    if(*str == '\0')
        return 0;
    while(*str == ' ')
        str++;
    int len = strlen(str);
    if(len >= MAX_BUFFER_SIZE){
        printf("  \033[31mError: The input must be less than %d characters, but now is %d.\033[0m\n", MAX_BUFFER_SIZE, len);
        return 1;
    }

    int argc = 0;
    char* argv_array[20];
    char* p = mystrtok(str, ' ');
    while(p){
        argv_array[argc++] = p;
        p = mystrtok(NULL, ' ');
    }
    argv_array[argc] = NULL;
    char** argv = argv_array;

    wrong_tag_t wrong_tag = 0;
    int one_cmd_argc;
    while((one_cmd_argc = find_and(argc, argv)) != -1){
        argv[one_cmd_argc] = NULL;
        if(one_cmd_argc > 0){
            if(strcmp(argv[0], "mkfs") == 0){
                wrong_tag += run_mkfs(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "statfs") == 0){
                wrong_tag += run_statfs(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "cd") == 0){
                wrong_tag += run_cd(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "mkdir") == 0){
                wrong_tag += run_mkdir(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "rmdir") == 0){
                wrong_tag += run_rmdir(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "ls") == 0){
                wrong_tag += run_ls(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "pwd") == 0) {
                wrong_tag += run_pwd(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "touch") == 0) {
                wrong_tag += run_touch(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "rmnod") == 0) {
                wrong_tag += run_rmnod(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "rm") == 0) {
                wrong_tag += run_rm(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "ln") == 0) {
                wrong_tag += run_ln(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "echo") == 0) {
                wrong_tag += run_echo(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "cat") == 0){
                wrong_tag += run_cat(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "quit") == 0){
                return 114514;
            } else {
                // wrong_tag += run_exec(one_cmd_argc, argv, 0);
                // if(wrong_tag == NO_SUCH_TASK)
                printf("  \033[31mCommand\033[0m '%s' \033[31mnot found.\033[0m\n", argv[0]);
            }
            if(wrong_tag != NO_ERROR)
                return (int)wrong_tag;
        }
        argc -= one_cmd_argc + 1;
        argv += one_cmd_argc + 1;
    }
    return (int)wrong_tag;
}

static void term_run()
{
    int c;
    char str[MAX_BUFFER_SIZE];
    int len = 0;
    char esc_buffer[10];
    int esc_buffer_index = 0;
    int esc_flag = 0;
    term_write();
    while(1){
        c = getchar();
        if (c == -1){
            continue;
        }
        if(esc_flag){
            if(c == '['){
                esc_buffer[esc_buffer_index++] = c;
            }
            else if(c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'H' || c == 'F'){
                esc_buffer[esc_buffer_index++] = c;
                esc_buffer[esc_buffer_index] = '\0';
                if(c == 'A'){
                    term_print_history(72, str, &len);
                }else if(c == 'B'){
                    term_print_history(80, str, &len);
                }
                esc_flag = 0;
            }
            continue;
        }
        if(c == 27){
            esc_flag = 1;
            esc_buffer_index = 0;
            continue;
        }
        if (c == 127 || c == '\b'){
            if (len > 0){
                len--;
                printf("\b \b");
            }
        }else if (c == '\r'|| c == '\n'){
            // printf("\n");
            if(len>0){
                str[len] = '\0';
                char str_copy[MAX_BUFFER_SIZE];
                strcpy(str_copy, str);
                if(term_analyze(str_copy) == 114514)
                    return;
                term_add_history(str, 1);
                len = 0;
            }
            term_write();
        }else if (len >= MAX_BUFFER_SIZE-1){
            continue;// too long input
        }else if (c>31 && c<127){
            // printf("%c", c);
            str[len++] = c;
        }else
            continue;
    }
}

int main() {
    printf("\n------------------------Welcome to GRFS!-----------------------------\n");
    init_io();
    init_fs();
    do_mkfs();
    do_statfs();
    // term_run();
    // int fd = do_open("test.txt", O_RDWR);
    // do_lseek(fd, 134217728 - 1, SEEK_SET);
    // do_write(fd, "\0", 1);
    term_run();
    release_io();
}