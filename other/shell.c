/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#define SHELL_BEGIN 20
#define SHELL_END 40
#define MAX_BUFFER_SIZE 50

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

static char* _strtok_index = NULL;
static char* strtok(char* str, char delim){
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

static wrong_tag_t run_ps(int argc, char** argv){
    if(argc > 1){
        printf("  [PS]\033[33m Warning: Donot need argument.\033[0m\n");
        return NORMAL_ERROR;
    }
    printf("  [PS] - Process are shown as follows:\n");
    sys_ps();
    return NO_ERROR;
}

static wrong_tag_t run_exec(int argc, char** argv, int active){
    if(argc == 0){
        printf("  [EXEC]\033[31m Need task name\033[0m\n");
        return NORMAL_ERROR;
    }
    int need_wait = 1;
    if(strcmp(argv[argc-1], "&") == 0){
        argv[argc-1] = NULL;
        argc--;
        need_wait = 0;
    }
    int pid = sys_exec(argv[0], argc, argv);
    if(pid == 0){
        if(active)
            printf("  [EXEC] (%s)\033[31m - No such task\033[0m\n", argv[0]);
        return NO_SUCH_TASK;
    }
    if(pid < 0){
        printf("  [EXEC] (%s)\033[31m - Failed to start task\033[0m\n", argv[0]);
        return NORMAL_ERROR;
    }
    printf("  [EXEC] (%s)\033[32m Task start with pid\033[0m %d\n", argv[0], pid);
    if(need_wait){
        sys_waitpid(pid);
        printf("  [%s]\033[32m Task exited\033[0m\n", argv[0]);
    }
    return NO_ERROR;
}

static wrong_tag_t run_kill(int argc, char** argv){
    if(argc == 1){
        printf("  [KILL]\033[31m - Invalid arguments\033[0m\n");
        printf("      Usage: kill [pid]\n");
        return NORMAL_ERROR;
    }
    int wrong_arg = 0;
    for(int i = 1; i < argc; i++){
        int pid = atoi(argv[i]);
        if(pid <= 0){
            printf("  [KILL] (%s)\033[31m arguments must be process or job IDs\033[0m\n", argv[i]);
            wrong_arg ++;
        }
        else if(!sys_kill(pid)){
            printf("  [KILL] (%d)\033[31m - No such process\033[0m\n", pid);
            wrong_arg ++;
        } else {
            printf("  [KILL] (%d)\033[32m Kill task success\033[0m\n", pid);
        }
    }
    if(wrong_arg <= argc - 2)
        return NO_ERROR;
    return NORMAL_ERROR;
}

static wrong_tag_t run_clear(int argc, char** argv){
    sys_clear();
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    if(argc != 1){
        printf("  [CLEAR]\033[33m Warning: Donot need argument.\033[0m\n");
        return NOREMAL_WARNING;
    }
    return NO_ERROR;
}

static wrong_tag_t run_help(int argc, char** argv){
    if(argc > 2){
        printf("  [HELP]\033[31m - too many arguments\033[0m\n");
        printf("      Usage: help [Options]\n");
        printf("  Options:\n");
        printf("      -d: show all available commands\n");
        printf("      -t: show all available tasks\n");
        return NORMAL_ERROR;
    } else if(argc == 2){
        if(strcmp(argv[1], "-d") == 0)
            run_help(1, NULL);
        else if(strcmp(argv[1], "-t") == 0){
            printf("  [HELP] - Available tasks:\n");
            sys_show_task();
        } else {
            printf("  [HELP]\033[31m - no help topics match '%s'  \033[0m\n", argv[1]);
            printf("      Usage: help [options]\n");
            printf("  Options:\n");
            printf("      -d: show all available commands\n");
            printf("      -t: show all available tasks\n");
            return NORMAL_ERROR;
        }
    } else if (argc == 1) {
        printf("  [HELP] - Available commands:\n");
        printf("    - ps                                        to show all process\n");
        printf("    - exec [task_name] [args...]                start a task\n");
        printf("    - kill [pid]                                kill a process or job\n");
        printf("    - clear                                     clear the screen\n");
        printf("    - taskset [options] [mask] [pid|task_name]  show or change the process affinity\n");
        printf("    - free                                      show memory usage\n");
        printf("    - help [options]                            show help information\n");
    }
    return NO_ERROR;
}

static wrong_tag_t run_taskset(int argc, char** argv){
    if(argc == 1 || argc > 4){
        printf("  [TASKSET]\033[31m - Invalid arguments\033[0m\n");
        printf("      Usage: taskset [options] [mask] [pid|task_name]\n");
        printf("  Options:\n");
        printf("      -p:  operate on existing given pid\n");
        printf("      -h:  display help information\n");
        return NORMAL_ERROR;
    }
    int has_p = 0;
    int has_h = 0;
    int now_argc = 1;
    if(argv[now_argc][0] == '-') {
        if(strcmp(argv[now_argc], "-p") == 0){
            has_p = 1;
            now_argc++;
        }
        else if(strcmp(argv[now_argc], "-h") == 0){
            has_h = 1;
            now_argc++;
        }
        else{
            printf("  [TASKSET]\033[31m - Invalid option\033[0m\n");
            printf("  Options:\n");
            printf("      -p:  operate on existing given pid\n");
            printf("      -h:  display help information\n");
            return NORMAL_ERROR;
        }
    }
    if(has_h){
        printf("      Usage: taskset [options] [mask] [pid|task_name]\n");
        printf("  Options:\n");
        printf("      -p:  operate on existing given pid\n");
        printf("      -h:  display help information\n");
        return NO_ERROR;
    }
    if((has_p && (argc - now_argc < 1 || argc - now_argc > 2)) || (!has_p && argc - now_argc < 2)){
        printf("  [TASKSET]\033[31m - Invalid arguments\033[0m\n");
        printf("      Usage: taskset [options] [mask] [pid|task_name]\n");
        printf("  Options:\n");
        printf("      -p:  operate on existing given pid\n");
        printf("      -h:  display help information\n");
        return NORMAL_ERROR;
    }
    int mask = atoi(argv[now_argc++]);
    if(has_p){
        int pid = atoi(argv[argc-1]);
        mask = sys_task_coremask(pid, mask, argc != now_argc);
        if(mask == 0){
            printf("  [TASKSET]\033[31m - No such process\033[0m\n");
            return NO_SUCH_TASK;
        } else if(argc != now_argc){
            printf("  [TASKSET] (%d) \033[32m - Set process affinity to %d\033[0m\n", pid, mask);
        } else {
            printf("  [TASKSET] (%d) Process affinity is %d\n", pid, mask);
        }
    } else {
        char* name = argv[now_argc];
        int pid = sys_exec_core(name, argc - now_argc, argv + now_argc, &mask);
        if(pid == 0){
            printf("  [EXEC] (%s)\033[31m - No such task\033[0m\n", argv[0]);
            return NO_SUCH_TASK;
        }
        if(pid < 0){
            printf("  [EXEC] (%s)\033[31m - Failed to start task\033[0m\n", argv[0]);
            return NORMAL_ERROR;
        }
        printf("  [TASKSET] (%s)\033[32m Task start with pid\033[0m %d\033[32m, affinity %x\033[0m\n", argv[0], pid, mask);
    }
    return NO_ERROR;
}

static wrong_tag_t run_free(int argc, char** argv){
    sys_showmem();
    if(argc != 1){
        printf("  [FREE]\033[33m Warning: Donot need argument.\033[0m\n");
        return NOREMAL_WARNING;
    }
    return NO_ERROR;
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
    printf("\033[1;32mtiny_os\033[31m@\033[1;32mUCAS\033[0m:\033[35m~\033[1;34m/root\033[0m > ");
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
    char* p = strtok(str, ' ');
    while(p){
        argv_array[argc++] = p;
        p = strtok(NULL, ' ');
    }
    argv_array[argc] = NULL;
    char** argv = argv_array;

    wrong_tag_t wrong_tag = 0;
    int one_cmd_argc;
    while((one_cmd_argc = find_and(argc, argv)) != -1){
        argv[one_cmd_argc] = NULL;
        if(one_cmd_argc > 0){
            if(strcmp(argv[0], "ps") == 0){
                wrong_tag += run_ps(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "exec") == 0){
                wrong_tag += run_exec(one_cmd_argc-1, argv+1, 1);
            } else if(strcmp(argv[0], "kill") == 0){
                wrong_tag += run_kill(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "clear") == 0){
                wrong_tag += run_clear(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "help") == 0){
                wrong_tag += run_help(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "taskset") == 0){
                wrong_tag += run_taskset(one_cmd_argc, argv);
            } else if(strcmp(argv[0], "free") == 0) {
                wrong_tag += run_free(one_cmd_argc, argv);
            } else {
                wrong_tag += run_exec(one_cmd_argc, argv, 0);
                if(wrong_tag == NO_SUCH_TASK)
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
        c = sys_getchar();
        if (c == -1){
            sys_yield();
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
            printf("\n");
            if(len>0){
                str[len] = '\0';
                char str_copy[MAX_BUFFER_SIZE];
                strcpy(str_copy, str);
                term_analyze(str_copy);
                term_add_history(str, 1);
                len = 0;
            }
            term_write();
        }else if (len >= MAX_BUFFER_SIZE-1){
            continue;// too long input
        }else if (c>31 && c<127){
            printf("%c", c);
            str[len++] = c;
        }else
            continue;
    }
}

int main(void)
{
    sys_move_cursor(0, SHELL_BEGIN);
    sys_set_scroll(SHELL_BEGIN+1, SHELL_END);
    printf("------------------- COMMAND -------------------\n");
    term_run();

    while (1)
    {

        /************************************************************/
        // TODO [P6-task1]: mkfs, statfs, cd, mkdir, rmdir, ls

        // TODO [P6-task2]: touch, cat, ln, ls -l, rm
        /************************************************************/
    }

    return 0;
}
