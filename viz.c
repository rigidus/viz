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
int *get_cells(tetris_piece_s piece, int *position);
void draw_piece(tetris_piece_s piece, int visible);
int position_ok(tetris_piece_s piece, int *playfield, int *position);
int move(tetris_piece_s *piece, int *playfield, int dx, int dy, int dz);
void flatten_piece(tetris_piece_s *piece, int *playfield);
void draw_playfield(int *playfield);
int line_complete(int line);
int process_complete_lines(int *playfield);
void update_score(int complete_lines);
void process_fallen_piece(tetris_piece_s *piece, int *playfield);
void cmd_right(tetris_piece_s *piece, int *playfield);
void cmd_left(tetris_piece_s *piece, int *playfield);
void cmd_rotate(tetris_piece_s *piece, int *playfield);
int cmd_down(tetris_piece_s *piece, int *playfield);
void cmd_drop(tetris_piece_s *piece, int *playfield);
void draw_help(int visible);
void draw_border();
tetris_piece_s get_next_piece(int visible);
void redraw_screen(int help_visible, tetris_piece_s next_piece, int next_visible, tetris_piece_s current_piece, int *playfield);
tetris_piece_s get_current_piece(tetris_piece_s next_piece, int *playfield);
long get_current_micros();
char get_key(long delay);
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
int *get_cells(tetris_piece_s piece, int *position) {
    static int cells[8] = {};
    int i = 0;
    int data = *(piece.data + piece.orientation);
    int x = piece.x;
    int y = piece.y;

    if (position) {
        x = *position;
        y = *(position + 1);
        data = *(piece.data + *(position + 2));
    }
    for (i = 0; i < 4; i++) {
        cells[2 * i] = x + ((data >> (4 * i)) & 3);
        cells[2 * i + 1] = y + ((data >> (4 * i + 2)) & 3);
    }
    return cells;
}
void draw_piece(tetris_piece_s piece, int visible) {
    int i = 0;
    int *cells = get_cells(piece, NULL);
    int x = 0;
    int y = 0;

    if (visible) {
        set_fg(piece.color);
        set_bg(piece.color);
    }
    for (i = 0; i < 4; i++) {
        x = cells[2 * i] * 2 + piece.origin_x;
        y = cells[2 * i + 1] + piece.origin_y;
        xyprint(x, y, visible ? FILLED_CELL : piece.empty_cell);
    }
    if (visible) {
        reset_colors();
    }
}
int position_ok(tetris_piece_s piece, int *playfield, int *position) {
    int i = 0;
    int x = 0;
    int y = 0;
    int *cells = get_cells(piece, position);

    for (i = 0; i < 4; i++) {
        x = *(cells + 2 * i);
        y = *(cells + 2 * i + 1);
        if (y < 0 || y >= PLAYFIELD_H || x < 0 || x >= PLAYFIELD_W || ((*(playfield + y) >> (x * 3)) & 7) != 0) {
            return 0;
        }
    }
    return 1;
}
int move(tetris_piece_s *piece, int *playfield, int dx, int dy, int dz) {
    int new_position[] = {piece->x + dx, piece->y + dy, (piece->orientation + dz) % piece->symmetry};

    if (position_ok(*piece, playfield, new_position)) {
        draw_piece(*piece, 0);
        piece->x = new_position[0];
        piece->y = new_position[1];
        piece->orientation = new_position[2];
        draw_piece(*piece, 1);
        return 1;
    }
    return (dy == 0);
}
void flatten_piece(tetris_piece_s *piece, int *playfield) {
    int i = 0;
    int x = 0;
    int y = 0;
    int *cells = get_cells(*piece, NULL);

    for (i = 0; i < 4; i++) {
        x = *(cells + 2 * i);
        y = *(cells + 2 * i + 1);
        *(playfield + y) |= (piece->color << (x * 3));
    }
}
void draw_playfield(int *playfield) {
    int x = 0;
    int y = 0;
    int color = 0;

    for (y = 0; y < PLAYFIELD_H; y++) {
        xyprint(PLAYFIELD_X, PLAYFIELD_Y + y, "");
        for (x = 0; x < PLAYFIELD_W; x++) {
            color = (*(playfield + y) >> (x * 3)) & 7;
            if (color) {
                set_bg(color);
                set_fg(color);
                printf(FILLED_CELL);
                reset_colors();
            } else {
                printf(PLAYFIELD_EMPTY_CELL);
            }
        }
    }
}
int line_complete(int line) {
    int i = 0;

    for (i = 0; i < PLAYFIELD_W; i++) {
        if (((line >> (i * 3)) & 7) == 0) {
            return 0;
        }
    }
    return 1;
}
int process_complete_lines(int *playfield) {
    int i = 0;
    int j = 0;
    int complete_lines = 0;

    for (i = 0; i < PLAYFIELD_H; i++) {
        if (line_complete(*(playfield + i))) {
            for (j = i; j > 0; j--) {
                *(playfield + j) = *(playfield + j - 1);
            }
            *playfield = 0;
            complete_lines++;
        }
    }
    return complete_lines;
}
void update_score(int complete_lines) {
    static int lines_completed = 0;
    static int score = 0;
    static int level = 1;
    char buf[64];

    lines_completed += complete_lines;
    score += (complete_lines * complete_lines);
    if (score > LEVEL_UP * level) {
        tetris_delay *= DELAY_FACTOR;
        level++;
    }
    set_bold();
    set_fg(SCORE_COLOR);
    sprintf(buf, "Lines completed: %d", lines_completed);
    xyprint(SCORE_X, SCORE_Y,     buf);
    sprintf(buf, "Level:           %d", level);
    xyprint(SCORE_X, SCORE_Y + 1, buf);
    sprintf(buf, "Score:           %d", score);
    xyprint(SCORE_X, SCORE_Y + 2, buf);
    reset_colors();
}
void process_fallen_piece(tetris_piece_s *piece, int *playfield) {
    int complete_lines = 0;

    flatten_piece(piece, playfield);
    complete_lines = process_complete_lines(playfield);
    if (complete_lines > 0) {
        update_score(complete_lines);
        draw_playfield(playfield);
    }
}
void cmd_right(tetris_piece_s *piece, int *playfield) {
    move(piece, playfield, 1, 0, 0);
}
void cmd_left(tetris_piece_s *piece, int *playfield) {
    move(piece, playfield, -1, 0, 0);
}
void cmd_rotate(tetris_piece_s *piece, int *playfield) {
    move(piece, playfield, 0, 0, 1);
}
int cmd_down(tetris_piece_s *piece, int *playfield) {
    if (move(piece, playfield, 0, 1, 0) == 1) {
        return 1;
    }
    process_fallen_piece(piece, playfield);
    return 0;
}
void cmd_drop(tetris_piece_s *piece, int *playfield) {
    while (cmd_down(piece, playfield)) {
    }
}
void draw_help(int visible) {
    char *text[] = {
        "  Use cursor keys",
        "       or",
        "    s: rotate",
        "a: left,  d: right",
        "    space: drop",
        "      q: quit",
        "  c: toggle color",
        "n: toggle show next",
        "h: toggle this help"
    };
    char spaces[] = "                   ";
    int i = 0;

    if (visible) {
        set_fg(HELP_COLOR);
        set_bold();
    }
    for (i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
        xyprint(HELP_X, HELP_Y + i, visible ? text[i] : spaces);
    }
    if (visible) {
        reset_colors();
    }
}
void draw_border() {
    int x1 = PLAYFIELD_X - 2;
    int x2 = PLAYFIELD_X + PLAYFIELD_W * 2;
    int i = 0;
    int y = 0;

    set_bold();
    set_fg(BORDER_COLOR);
    for (i = 0; i < PLAYFIELD_H + 1; i++) {
        y = i + PLAYFIELD_Y;
        xyprint(x1, y, "<|");
        xyprint(x2, y, "|>");
    }

    y = PLAYFIELD_Y + PLAYFIELD_H;
    for (i = 0; i < PLAYFIELD_W; i++) {
        x1 = i * 2 + PLAYFIELD_X;
        xyprint(x1, y, "==");
        xyprint(x1, y + 1, "\\/");
    }
    reset_colors();
}
tetris_piece_s get_next_piece(int visible) {
    static int square_data[] = { 1, 0x1256 };
    static int line_data[] = { 2, 0x159d, 0x4567 };
    static int s_data[] = { 2, 0x4512, 0x0459 };
    static int z_data[] = { 2, 0x0156, 0x1548 };
    static int l_data[] = { 4, 0x159a, 0x8456, 0x0159, 0x2654 };
    static int r_data[] = { 4, 0x1598, 0x0456, 0x2159, 0xa654 };
    static int t_data[] = { 4, 0x1456, 0x1596, 0x4569, 0x4159 };
    static int *piece_data[] = {
        square_data,
        line_data,
        s_data,
        z_data,
        l_data,
        r_data,
        t_data
    };
    static int piece_data_len = sizeof(piece_data) / sizeof(piece_data[0]);
    static int colors[] = { RED, GREEN, YELLOW, BLUE, FUCHSIA, CYAN, WHITE};
    int next_piece_index = random() % piece_data_len;
    int *next_piece_data = piece_data[next_piece_index];
    tetris_piece_s next_piece;

    next_piece.origin_x = NEXT_X;
    next_piece.origin_y = NEXT_Y;
    next_piece.x = 0;
    next_piece.y = 0;
    next_piece.color = colors[random() % (sizeof(colors) / sizeof(colors[0]))];
    next_piece.data = next_piece_data + 1;
    next_piece.symmetry = *next_piece_data;
    next_piece.orientation = random() % next_piece.symmetry;
    strcpy(next_piece.empty_cell, NEXT_EMPTY_CELL);
    draw_piece(next_piece, visible);
    return next_piece;
}
void redraw_screen(int help_visible, tetris_piece_s next_piece, int next_visible, tetris_piece_s current_piece, int *playfield) {
    clear_screen();
    draw_help(help_visible);
    update_score(0);
    draw_border();
    draw_playfield(playfield);
    draw_piece(next_piece, next_visible);
    draw_piece(current_piece, 1);
}
tetris_piece_s get_current_piece(tetris_piece_s next_piece, int *playfield) {
    tetris_piece_s current_piece = next_piece;
    current_piece.x = (PLAYFIELD_W - 4) / 2;
    current_piece.y = 0;
    current_piece.origin_x = PLAYFIELD_X;
    current_piece.origin_y = PLAYFIELD_Y;
    strcpy(current_piece.empty_cell, PLAYFIELD_EMPTY_CELL);
    if (!position_ok(current_piece, playfield, NULL)) {
        cmd_quit();
    }
    draw_piece(next_piece, 0);
    draw_piece(current_piece, 1);
    return current_piece;
}
long get_current_micros() {
    struct timeval t;

    gettimeofday(&t, NULL);
    return t.tv_usec + t.tv_sec * 1000000;
}
char get_key(long delay) {
    char buf[16] = {};
    static int buf_len = 0;
    static int buf_pos = 0;

    /* Если буфер не пуст - продолжаем возвращать из него, */
    /* без ожидания, пока не опустошим */
    if (buf_len > 0 && buf_pos < buf_len) {
        return buf[buf_pos++];
    }
    /* здесь буфер пуст, поэтому обнуляем его к исходному */
    buf_len = 0;
    buf_pos = 0;

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

    /* вложеная функция чтения из пайпа */
    void read_and_show_pipe () {
        char pipe_buf[65535] = {0}; // initialization by zeros
        int pipe_buf_len = read(fdfifo, pipe_buf, 65535);
        if (0 > pipe_buf_len) {
            error_exit(errno);
        }
        char tmp[65535];
        sprintf(tmp, "%s", pipe_buf);
        xyprint(0, 29, tmp);
    }

    /* вложенная функция чтения из stdin */
    void read_and_show_stdin () {
        buf_len = read(STDIN_FILENO, buf, 16);
        if (0 > buf_len) {
            error_exit(errno);
        }
        char tmp[80];
        int tmp_len =
            sprintf(tmp,
                    "%02X.%02X.%02X.%02X:%02X.%02X.%02X.%02X:%02X.%02X.%02X.%02X:%02X.%02X.%02X.%02X",
                    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
                    buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
                    buf[12], buf[13], buf[14], buf[15]);
        xyprint(0, 25, tmp);
        /* пишем в отдельный fifo */
        write(fdfifo_ctrl, tmp, tmp_len);
        write(fdfifo_ctrl, "\n", 1); /* need for line-buferization */
        fsync(fdfifo_ctrl);
    }

    if (stdin_flag) {
        xyprint(SCORE_X, SCORE_Y + 3, "stdin_flag");
    } else {
        xyprint(SCORE_X, SCORE_Y + 3, "          ");
    }

    if (fifo_flag) {
        xyprint(SCORE_X+12, SCORE_Y + 3, "fifo_flag");
    } else {
        xyprint(SCORE_X+12, SCORE_Y + 3, "         ");
    }

    if (!fifo_flag && !stdin_flag) {
        /* таймаут, вернем ноль */
        xyprint(SCORE_X, SCORE_Y + 4, "status: timeout");
        return 0;
    } else if (fifo_flag && !stdin_flag) {
        /* что-то пришло в пайп, а stdin пустой */
        /* прочтем и отобразим содержимое пайпа и вернем ноль */
        xyprint(SCORE_X, SCORE_Y + 4, "status: pipe   ");
        read_and_show_pipe();
        return 0;
    } else if (!fifo_flag && stdin_flag) {
        /* что-то пришло в stdin, а пайп пустой */
        /* прочтем в буфер и вернем первый символ */
        xyprint(SCORE_X, SCORE_Y + 4, "status: stdin  ");
        read_and_show_stdin();
        return buf[buf_pos++];
    } else if (fifo_flag && stdin_flag){
        /* одновременно есть что-то и в stdin и в пайпе */
        /* читаем и отображаем все и возвращаем первый символ */
        xyprint(SCORE_X, SCORE_Y + 4, "status: both   ");
        read_and_show_pipe();
        read_and_show_stdin();
        return buf[buf_pos++];
    }

    /* тут мы не должны оказаться ни при каких обстоятельствах */
    xyprint(SCORE_X, SCORE_Y + 4, "status: pipets");
    error_exit(errno);
    return 0;
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
    int help_visible = 1;
    int next_visible = 1;
    tetris_piece_s next_piece;
    tetris_piece_s current_piece;
    int playfield[PLAYFIELD_H] = {};
    int i = 0;
    int flags = fcntl(STDOUT_FILENO, F_GETFL);
    long last_down_time = 0;
    long now = 0;
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
    for (i = 0; i < PLAYFIELD_H; i++) {
        playfield[i] = 0;
    }
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
    /* init data */
    next_piece = get_next_piece(next_visible);
    current_piece = get_current_piece(next_piece, playfield);
    next_piece = get_next_piece(next_visible);
    /* redraw screen */
    redraw_screen(help_visible, next_piece, next_visible, current_piece, playfield);
    fflush(stdout);
    /* loop */
    while(1) {
        now = get_current_micros();
        c = get_key(last_down_time + tetris_delay - now);
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
        /* case 'C': */
        /* case 'd': */
        /*     cmd_right(&current_piece, playfield); */
        /*     break; */
        /* case 'D': */
        /* case 'a': */
        /*     cmd_left(&current_piece, playfield); */
        /*     break; */
        /* case 'A': */
        /* case 's': */
        /*     cmd_rotate(&current_piece, playfield); */
        /*     break; */
        case 0:
            last_down_time = get_current_micros();
            /* if (!cmd_down(&current_piece, playfield)) { */
            /*     current_piece = get_current_piece(next_piece, playfield); */
            /*     next_piece = get_next_piece(next_visible); */
            /* } */
            break;
        case ' ':
            cmd_drop(&current_piece, playfield);
            current_piece = get_current_piece(next_piece, playfield);
            next_piece = get_next_piece(next_visible);
            break;
        case 'h':
            help_visible ^= 1;
            draw_help(help_visible);
            break;
        case 'n':
            next_visible ^= 1;
            draw_piece(next_piece, next_visible);
            break;
        case 'c':
            use_color ^= 1;
            redraw_screen(help_visible, next_piece, next_visible, current_piece, playfield);
            break;
        default:
            break;
        }
        fflush(stdout);
     }
}
