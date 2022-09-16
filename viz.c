/* INCLUDES */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

/* DEFINES */
#define ESC 27

#define DELAY 1
#define DELAY_FACTOR 0.8

#define RED 1
#define GREEN 2
#define YELLOW 3
#define BLUE 4
#define FUCHSIA 5
#define CYAN 6
#define WHITE 7

#define PLAYFIELD_W 10
#define PLAYFIELD_H 20
#define PLAYFIELD_X 30
#define PLAYFIELD_Y 1
#define BORDER_COLOR YELLOW

#define SCORE_X 1
#define SCORE_Y 2
#define SCORE_COLOR GREEN

#define HELP_X 58
#define HELP_Y 1
#define HELP_COLOR CYAN

#define NEXT_X 14
#define NEXT_Y 11

#define GAMEOVER_X 1
#define GAMEOVER_Y (PLAYFIELD_H + 3)

#define LEVEL_UP 20

#define FILLED_CELL "[]"
#define NEXT_EMPTY_CELL "  "
#define PLAYFIELD_EMPTY_CELL " ."

/* STRUCTURES */
struct termios terminal_conf;
int use_color = 1;
long tetris_delay = DELAY * 1000000;

typedef struct {
    int origin_x;
    int origin_y;
    int x;
    int y;
    int color;
    int symmetry;
    int orientation;
    int *data;
    char empty_cell[3];
} tetris_piece_s;

/* DECLARATIONS */
void set_fg(int color);
void set_bg(int color);
void reset_colors();
void clear_screen();
void xyprint(int x, int y, char *s);
void set_bold();
void unset_bold();
void my_exit (int retcode);
void error_exit(int errsv);
void cmd_quit();
long get_current_micros();
void cursor_control_on();
void cursor_control_off();
void mouse_control_on();
void mouse_control_off();

/* GLOBALS */
int  flag_cursor_control = 0;
int  flag_mouse_control = 0;
char myfifo[] = "test-pipe";
int  fdfifo = 0;
char myfifo_ctrl[] = "test-pipe-ctrl";
int  fdfifo_ctrl = 0;

/* FUNCTIONS */
void set_fg(int color) {
    if (use_color) {
        printf("\033[3%dm", color);
    }
}
void set_bg(int color) {
    if (use_color) {
        printf("\033[4%dm", color);
    }
}
void reset_colors() {
    printf("\033[0m");
}
void clear_screen() {
    printf("\033[2J");
}
void xyprint(int x, int y, char *s) {
    printf("\033[%d;%dH%s", y, x, s);
}
void set_bold() {
    printf("\033[1m");
}
void unset_bold() {
    printf("\033[0m");
}
void my_exit (int retcode) {
    int flags = fcntl(STDOUT_FILENO, F_GETFL);
    fcntl(STDOUT_FILENO, F_SETFL, flags & (~O_NONBLOCK));
    tcsetattr(STDIN_FILENO, TCSANOW, &terminal_conf);
    if (flag_cursor_control) {
        cursor_control_off();
    }
    if (flag_mouse_control) {
        mouse_control_off();
    }
    if (fdfifo) {
        close(fdfifo);
    }
      if (fdfifo_ctrl) {
        close(fdfifo_ctrl);
    }
    exit(retcode);
}
void error_exit(int errsv) {
    if (EINTR == errsv) {
    } else if (EAGAIN == errsv) {
    } else if (EWOULDBLOCK == errsv) {
    } else {
        /* close descriptor immediately */
    }
    xyprint(1,1, strerror(errsv));
    my_exit(EXIT_FAILURE);
}
void cmd_quit() {
    xyprint(GAMEOVER_X, GAMEOVER_Y, "Game over!");
    xyprint(GAMEOVER_X, GAMEOVER_Y + 1, "");
    my_exit(0);
}
long get_current_micros() {
    struct timeval t;

    gettimeofday(&t, NULL);
    return t.tv_usec + t.tv_sec * 1000000;
}
void cursor_control_on() {
    printf("\033[?25l");
    printf("\033[2;1'z"); /* switch on pixel resolution: \e[2;1'z */
    flag_cursor_control = 1;
}
void cursor_control_off() {
    printf("\033[?25h");
    flag_cursor_control = 0;
}
void mouse_control_on() {
    printf("\033[?1000h");
    flag_mouse_control = 1;
}
void mouse_control_off() {
    printf("\033[?1000l");
    flag_mouse_control = 0;
}

/* MAIN */
int main(int argc, char* argv[]) {
    char c = 0;
    char key[] = {0, 0, 0};
    tcflag_t c_lflag_orig = 0;
    int i = 0;
    int flags = fcntl(STDOUT_FILENO, F_GETFL);
    long last_down_time = 0;
    /* Run under XTerm only */
    /* or sixel support : https://stackoverflow.com/questions/18379477/how-to-interpret-response-from-vt-100-vt-102-da-request/18380004#18380004 */
    int   xterm = 0;
    char* term = getenv("TERM");
    if (term) {
        if (0 == strcmp("dumb", term))  {
            printf("Error: This program does not work under dumb terminal!\n");
        } else if (0 == strcmp("xterm", term))  {
            xterm = 1;
        }
    }
    if (!xterm) {
        printf("Error: This program run under XTerm only!\n");
        return -1;
    }
    /* set non-block on stdout */
    fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
    /* read stdin configuration to termios struct */
    tcgetattr(STDIN_FILENO, &terminal_conf);
    /* save original local modes */
    c_lflag_orig = terminal_conf.c_lflag;
    /* canonical mode off, echo off */
    terminal_conf.c_lflag &= ~(ICANON | ECHO);
    /* set updated termios struct */
    tcsetattr(STDIN_FILENO, TCSANOW, &terminal_conf);
    /* copy original local modes field to the termios struct */
    terminal_conf.c_lflag = c_lflag_orig;
    /* */
    last_down_time = get_current_micros();
    srandom(time(NULL));
    /* init cursor */
    cursor_control_on();
    /* init mouse */
    mouse_control_on();
    /* remove if exist, create and open named non-block pipe */
    if (access(myfifo, F_OK) == 0) {
        remove(myfifo);
    }
    if (-1 == mkfifo(myfifo, 0777)) {
        perror("Error while creating the pipe.\n");
        my_exit(EXIT_FAILURE);
    }
    if (chmod (myfifo, 0777) < 0)
    {
        fprintf(stderr, "Error: chmod pipe - %d (%s)\n", errno, strerror(errno));
        my_exit(EXIT_FAILURE);
    }
    fdfifo = open(myfifo, O_RDWR | O_NONBLOCK);
    /* control fifo | TODO: O_READ & 0_WRITE */
    if (access(myfifo_ctrl, F_OK) == 0) {
        remove(myfifo_ctrl);
    }
    if (-1 == mkfifo(myfifo_ctrl, 0777)) {
        perror("Error while creating the pipe control.\n");
        my_exit(EXIT_FAILURE);
    }
    if (chmod (myfifo_ctrl, 0777) < 0)
    {
        fprintf(stderr, "Error: chmod pipe - %d (%s)\n", errno, strerror(errno));
        my_exit(EXIT_FAILURE);
    }
    fdfifo_ctrl = open(myfifo_ctrl, O_RDWR);
    /* loop */
    while(1) {
        long delay = last_down_time + tetris_delay - get_current_micros();
    
        char stdi_buf[16] = {0};
    
        /* DELAY */
    
        struct timeval tv;
        fd_set fs;
    
        /* заполняем структуру ожидания */
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (delay > 0) {
            tv.tv_sec = delay / 1000000;
            tv.tv_usec = delay % 1000000;
        }
    
        /* ожидаем на select-e */
        /* здесь мы неявно предполагаем, что fdfifo больше stdin */
        FD_ZERO(&fs);                 /* clear a set */
        FD_SET(STDIN_FILENO, &fs);    /* add stdin */
        FD_SET(fdfifo, &fs);          /* add fdfifo */
        int nfds = fdfifo + 1;        /* вместо fdfifo + 1 */
        select(nfds, &fs, 0, 0, &tv);
    
        /* тут мы оказываемся, если что-то пришло или таймаут */
        int fifo_flag = FD_ISSET(fdfifo, &fs);
        int stdin_flag = FD_ISSET(STDIN_FILENO, &fs);
    
        /* DELAY END */
    
        if (fifo_flag) {
            char pipe_buf[65535] = {0}; // initialization by zeros
            int pipe_buf_len = read(fdfifo, pipe_buf, sizeof(pipe_buf));
            if (0 > pipe_buf_len) {
                printf("err: read_and_show_pipe()"); // replace to show_error()
                error_exit(errno);
            }
            char tmp[65535] = {0};
            sprintf(tmp, "%s", pipe_buf);
            xyprint(0, 29, tmp);
        }
    
        if (stdin_flag) {
            int stdi_buf_len = read(STDIN_FILENO, stdi_buf, sizeof(stdi_buf));
            if (0 > stdi_buf_len) {
                printf("err: read_and_show_stdin()"); // replace to show_error()
                error_exit(errno);
            }
            char tmp[1024];
            int tmp_len =
                sprintf(tmp,
                        "%02X.%02X.%02X.%02X:%02X.%02X.%02X.%02X:%02X.%02X.%02X.%02X:%02X.%02X.%02X.%02X",
                        stdi_buf[0], stdi_buf[1], stdi_buf[2], stdi_buf[3], stdi_buf[4],
                        stdi_buf[5], stdi_buf[6], stdi_buf[7], stdi_buf[8], stdi_buf[9],
                        stdi_buf[10], stdi_buf[11], stdi_buf[12], stdi_buf[13],
                        stdi_buf[14], stdi_buf[15]);
            xyprint(0, 25, tmp);
            /* пишем в отдельный fifo */
            write(fdfifo_ctrl, tmp, tmp_len);
            write(fdfifo_ctrl, "\n", 1); /* need for line-buferization */
            fsync(fdfifo_ctrl);
        }
    
        // -------------
    
        c = stdi_buf[0];
        key[2] = key[1];
        key[1] = key[0];
        if (key[2] == ESC && key[1] == '[') {
            key[0] = c;
        } else {
            key[0] = tolower(c);
        }
        switch(key[0]) {
        case 3:
        case 'q':
            cmd_quit();
            break;
        case 0:
            last_down_time = get_current_micros();
            /* if (!cmd_down(&current_piece, playfield)) { */
            /*     current_piece = get_current_piece(next_piece, playfield); */
            /*     next_piece = get_next_piece(next_visible); */
            /* } */
            break;
        default:
            break;
        }
        fflush(stdout);
     }
}
