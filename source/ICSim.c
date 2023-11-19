#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_ttf.h>
#include <stdbool.h>

#ifndef DATA_DIR
#define DATA_DIR "./img/"
#endif

#define DEFAULT_battery_ID 700
#define DEFAULT_charge_ID 001
#define DEFAULT_save_ID 950
#define DEFAULT_AC_ID 800
#define DEFAULT_brake_ID 600
#define DEFAULT_park_ID 900
#define DEFAULT_BYTE 0

#define SCREEN_WIDTH 692
#define SCREEN_HEIGHT 329

#define DOOR_LOCKED 0
#define DOOR_UNLOCKED 1
#define OFF 0
#define ON 1
#define DEFAULT_DOOR_ID 411 // 0x19b
#define DEFAULT_DOOR_BYTE 2
#define CAN_DOOR1_LOCK 1
#define CAN_DOOR2_LOCK 2
#define CAN_DOOR3_LOCK 4
#define CAN_DOOR4_LOCK 8
#define DEFAULT_SIGNAL_ID 392 // 0x188
#define DEFAULT_SIGNAL_BYTE 0
#define CAN_LEFT_SIGNAL 1
#define CAN_RIGHT_SIGNAL 2
#define DEFAULT_SPEED_ID 580 // 0x244
#define DEFAULT_SPEED_BYTE 3 // bytes 3,4

int can;
const int canfd_on = 1;
char data_file[256];
//ECU status==========================
int ac = 0, battery = 0, seatbelt = 0, brake = 0, park = 0;
long current_speed = 0;
int door_status[4];
int turn_status[2];
//frame data position=================
int door_pos = DEFAULT_DOOR_BYTE;
int signal_pos = DEFAULT_SIGNAL_BYTE;
int speed_pos = DEFAULT_SPEED_BYTE;
int AC_pos = DEFAULT_BYTE;
int battery_pos = DEFAULT_BYTE;
int seatbelt_pos = DEFAULT_BYTE;
int brake_pos = DEFAULT_BYTE;
int park_pos = DEFAULT_BYTE;
//dashboard texture====================
SDL_Renderer *renderer = NULL;
SDL_Texture *base_texture = NULL;
SDL_Texture *needle_tex = NULL;
SDL_Texture *sprite_tex = NULL;
SDL_Rect speed_rect;
//dashboard indicator light texture====
SDL_Texture *AC_black_tex = NULL;
SDL_Texture *AC_white_tex = NULL;
SDL_Texture *battery_empty_tex = NULL;
SDL_Texture *battery_green_tex = NULL;
SDL_Texture *brake_red_tex = NULL;
SDL_Texture *brake_yellow_tex = NULL;
SDL_Texture *brake_white_tex = NULL;
SDL_Texture *park_red_tex = NULL;
SDL_Texture *park_yellow_tex = NULL;
SDL_Texture *park_white_tex = NULL;
SDL_Texture *seatbelt_red_tex = NULL;
SDL_Texture *seatbelt_white_tex = NULL;
SDL_Texture *degree_tex = NULL;
SDL_Texture *logo_tex = NULL;
//battery SDL & font==================
int power = 100, times = 0;;
bool charge = false;
char* power_string = NULL;
SDL_Surface *font_surface;
TTF_Font *font;
SDL_Color color= {255, 255, 255};
SDL_Texture *power_font_texture = NULL;
//dashboard indicator light position====
SDL_Rect battery_green_rect = { 50, 160, 80, 160};
SDL_Rect battery_empty_rect = { 50, 160, 80, 160};
SDL_Rect font_rect = { 50, 80, 80, 80 };

SDL_Rect AC_rect = { 0, 0, 140, 80};
SDL_Rect degree_rect = { 530, 0, 140, 70};
SDL_Rect brake_rect = { 600, 70, 70, 70};
SDL_Rect park_rect = { 600, 150, 70, 70};
SDL_Rect seatbelt_rect = { 600, 230, 80, 80};
SDL_Rect logo_rect = {250, 230, 80, 80};

//CarData==============================
int CarData_index = 0;
struct CarData {
    int cost;
    int mileage;
    int car_speed;
    int driving_time;
    int inside_temp;
    int outside_temp;
    int battery_health;
    int co2;
};

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

char *get_data(char *fname) {
    if(strlen(DATA_DIR) + strlen(fname) > 255) return NULL;
    strncpy(data_file, DATA_DIR, 255);
    strncat(data_file, fname, 255-strlen(data_file));
    return data_file;
}

void init_car_state() {
    door_status[0] = DOOR_LOCKED;
    door_status[1] = DOOR_LOCKED;
    door_status[2] = DOOR_LOCKED;
    door_status[3] = DOOR_LOCKED;
    turn_status[0] = OFF;
    turn_status[1] = OFF;
}

void blank_ic() {
    SDL_RenderCopy(renderer, base_texture, NULL, NULL);
}

void update_speed() {
    SDL_Rect dial_rect;
    SDL_Point center;
    double angle = 0;
    dial_rect.x = 200;
    dial_rect.y = 80;
    dial_rect.h = 130;
    dial_rect.w = 300;
    SDL_RenderCopy(renderer, base_texture, &dial_rect, &dial_rect);
    /* Because it's a curve we do a smaller rect for the top */
    dial_rect.x = 250;
    dial_rect.y = 30;
    dial_rect.h = 65;
    dial_rect.w = 200;
    SDL_RenderCopy(renderer, base_texture, &dial_rect, &dial_rect);
    // And one more smaller box for the pivot point of the needle
    dial_rect.x = 323;
    dial_rect.y = 171;
    dial_rect.h = 52;
    dial_rect.w = 47;
    SDL_RenderCopy(renderer, base_texture, &dial_rect, &dial_rect);
    center.x = 135;
    center.y = 20;
    angle = map(current_speed, 0, 280, 0, 180);
    if(angle < 0) angle = 0;
    if(angle > 180) angle = 180;
    SDL_RenderCopyEx(renderer, needle_tex, NULL, &speed_rect, angle, &center, SDL_FLIP_NONE);
}

void update_AC() {
    if(ac == 0) {
        SDL_RenderFillRect(renderer, &AC_rect);
        SDL_RenderCopy(renderer, AC_black_tex, NULL, &AC_rect);
    } else if(ac == 1) {
        SDL_RenderFillRect(renderer, &AC_rect);
        SDL_RenderCopy(renderer, AC_white_tex, NULL, &AC_rect);
    }
}

void update_degree() {
    SDL_RenderFillRect(renderer, &degree_rect);
    SDL_RenderCopy(renderer, degree_tex, NULL, &degree_rect);
}

void update_logo() {
    SDL_RenderFillRect(renderer, &logo_rect);
    SDL_RenderCopy(renderer, logo_tex, NULL, &logo_rect);
}

void update_brake() {
    if(brake == 0) { //close
        SDL_RenderFillRect(renderer, &brake_rect);
        SDL_RenderCopy(renderer, brake_white_tex, NULL, &brake_rect);     
    } else if(brake == 1) { //yellow
        SDL_RenderFillRect(renderer, &brake_rect);
        SDL_RenderCopy(renderer, brake_yellow_tex, NULL, &brake_rect);
    } else if(brake == 2) { //red
        SDL_RenderFillRect(renderer, &brake_rect);
        SDL_RenderCopy(renderer, brake_red_tex, NULL, &brake_rect);
    }
}

void update_seatbelt() {
    if(seatbelt == 0) {
        SDL_RenderFillRect(renderer, &seatbelt_rect);
    	SDL_RenderCopy(renderer, seatbelt_white_tex, NULL, &seatbelt_rect);
    } else if (seatbelt == 1) {
        SDL_RenderFillRect(renderer, &seatbelt_rect);
        SDL_RenderCopy(renderer, seatbelt_red_tex, NULL, &seatbelt_rect);
    }
}

void update_park() {
    if(park == 0) {
        SDL_RenderFillRect(renderer, &park_rect);
        SDL_RenderCopy(renderer, park_white_tex, NULL, &park_rect);
    } else if(park == 1) {
        SDL_RenderFillRect(renderer, &park_rect);
        SDL_RenderCopy(renderer, park_yellow_tex, NULL, &park_rect);
    } else if(park == 2) {
        SDL_RenderFillRect(renderer, &park_rect);
        SDL_RenderCopy(renderer, park_red_tex, NULL, &park_rect);
    }
}

void update_battery() {
	SDL_RenderCopy(renderer, base_texture, &battery_empty_rect, &battery_empty_rect); //redraw
    SDL_RenderCopy(renderer, battery_green_tex, NULL, &battery_green_rect);
    SDL_RenderCopy(renderer, battery_empty_tex, NULL, &battery_empty_rect);

    SDL_RenderCopy(renderer, base_texture, &font_rect, &font_rect);
    SDL_RenderCopy(renderer, power_font_texture, NULL, &font_rect);
}

void update_doors() {
    SDL_Rect door_area, update, pos;
    door_area.x = 390;
    door_area.y = 215;
    door_area.w = 110;
    door_area.h = 85;
    SDL_RenderCopy(renderer, base_texture, &door_area, &door_area);
    // No update if all doors are locked
    if(door_status[0] == DOOR_LOCKED && door_status[1] == DOOR_LOCKED &&
            door_status[2] == DOOR_LOCKED && door_status[3] == DOOR_LOCKED) return;
    // Make the base body red if even one door is unlocked
    update.x = 440;
    update.y = 239;
    update.w = 45;
    update.h = 83;
    memcpy(&pos, &update, sizeof(SDL_Rect));
    pos.x -= 22;
    pos.y -= 22;
    SDL_RenderCopy(renderer, sprite_tex, &update, &pos);
    if(door_status[0] == DOOR_UNLOCKED) {
        update.x = 420;
        update.y = 263;
        update.w = 21;
        update.h = 22;
        memcpy(&pos, &update, sizeof(SDL_Rect));
        pos.x -= 22;
        pos.y -= 22;
        SDL_RenderCopy(renderer, sprite_tex, &update, &pos);
    }
    if(door_status[1] == DOOR_UNLOCKED) {
        update.x = 484;
        update.y = 261;
        update.w = 21;
        update.h = 22;
        memcpy(&pos, &update, sizeof(SDL_Rect));
        pos.x -= 22;
        pos.y -= 22;
        SDL_RenderCopy(renderer, sprite_tex, &update, &pos);
    }
    if(door_status[2] == DOOR_UNLOCKED) {
        update.x = 420;
        update.y = 284;
        update.w = 21;
        update.h = 22;
        memcpy(&pos, &update, sizeof(SDL_Rect));
        pos.x -= 22;
        pos.y -= 22;
        SDL_RenderCopy(renderer, sprite_tex, &update, &pos);
    }
    if(door_status[3] == DOOR_UNLOCKED) {
        update.x = 484;
        update.y = 287;
        update.w = 21;
        update.h = 22;
        memcpy(&pos, &update, sizeof(SDL_Rect));
        pos.x -= 22;
        pos.y -= 22;
        SDL_RenderCopy(renderer, sprite_tex, &update, &pos);
    }
}

void update_turn_signals() {
    SDL_Rect left, right, lpos, rpos;
    left.x = 213;
    left.y = 51;
    left.w = 45;
    left.h = 45;
    memcpy(&right, &left, sizeof(SDL_Rect));
    right.x = 482;
    memcpy(&lpos, &left, sizeof(SDL_Rect));
    memcpy(&rpos, &right, sizeof(SDL_Rect));
    lpos.x -= 22;
    lpos.y -= 22;
    rpos.x -= 22;
    rpos.y -= 22;
    if(turn_status[0] == OFF) {
        SDL_RenderCopy(renderer, base_texture, &lpos, &lpos);
    } else {
        SDL_RenderCopy(renderer, sprite_tex, &left, &lpos);
    }
    if(turn_status[1] == OFF) {
        SDL_RenderCopy(renderer, base_texture, &rpos, &rpos);
    } else {
        SDL_RenderCopy(renderer, sprite_tex, &right, &rpos);
    }
}

void redraw_ic() {
    blank_ic();
    update_speed();
    update_doors();
    update_turn_signals();
    update_AC();
    update_battery();
    update_brake();
    update_seatbelt();
    update_park();
    update_degree();
    update_logo();

    SDL_RenderPresent(renderer);
}

void update_brake_state(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < brake_pos) return;

    brake = cf->data[brake_pos];

    update_brake();
    SDL_RenderPresent(renderer);
}

void update_park_state(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < park_pos) return;

    park = cf->data[park_pos];

    update_park();
    SDL_RenderPresent(renderer);
}

void update_AC_state(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < AC_pos) return;

    ac = cf->data[AC_pos];

    update_AC();
    SDL_RenderPresent(renderer);
}

void update_seatbelt_state(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < seatbelt_pos) return;

    seatbelt = cf->data[seatbelt_pos];

    update_seatbelt();
    SDL_RenderPresent(renderer);
}

void update_battery_state(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < battery_pos) return;

    if(cf->data[battery_pos] == 1) {
    	battery_green_rect.y = 320;
        battery_green_rect.h = 0;
        power = 0;
    } else if(cf->data[battery_pos] == 0) {
        battery_green_rect.y = 160;
        battery_green_rect.h = 160;
        power = 100;
    }

    power_string = (char*)malloc(5 * sizeof(char)); 
    sprintf(power_string, "%d%%", power);
    TTF_SizeUTF8(font, power_string, 0, 0);
    font_surface = TTF_RenderUTF8_Solid(font, power_string, color);
    power_font_texture = SDL_CreateTextureFromSurface(renderer, font_surface);

    update_battery();
    SDL_RenderPresent(renderer);
}

void update_speed_status(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < speed_pos + 1) return;

    int speed = cf->data[speed_pos] << 8;
    speed += cf->data[speed_pos + 1];
    speed = speed / 100; // speed in kilometers
    current_speed = speed * 0.6213751; // mph

    update_speed();
    SDL_RenderPresent(renderer);
}

void update_signal_status(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < signal_pos) return;
    if(cf->data[signal_pos] & CAN_LEFT_SIGNAL) {
        turn_status[0] = ON;
    } else {
        turn_status[0] = OFF;
    }
    if(cf->data[signal_pos] & CAN_RIGHT_SIGNAL) {
        turn_status[1] = ON;
    } else {
        turn_status[1] = OFF;
    }
    update_turn_signals();
    SDL_RenderPresent(renderer);
}

void update_door_status(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < door_pos) return;
    if(cf->data[door_pos] & CAN_DOOR1_LOCK) {
        door_status[0] = DOOR_LOCKED;
    } else {
        door_status[0] = DOOR_UNLOCKED;
    }
    if(cf->data[door_pos] & CAN_DOOR2_LOCK) {
        door_status[1] = DOOR_LOCKED;
    } else {
        door_status[1] = DOOR_UNLOCKED;
    }
    if(cf->data[door_pos] & CAN_DOOR3_LOCK) {
        door_status[2] = DOOR_LOCKED;
    } else {
        door_status[2] = DOOR_UNLOCKED;
    }
    if(cf->data[door_pos] & CAN_DOOR4_LOCK) {
        door_status[3] = DOOR_LOCKED;
    } else {
        door_status[3] = DOOR_UNLOCKED;
    }
    update_doors();
    SDL_RenderPresent(renderer);
}

void update_charge_state(struct canfd_frame *cf, int maxdlen) {
    int len = (cf->len > maxdlen) ? maxdlen : cf->len;
    if(len < battery_pos) return;

    if(cf->data[battery_pos] == 1) { 
    	charge = true;
    	int charge_time = (100-power)/10;
    	if(power%10 > 0) charge_time++;

    	//return charge times
    	struct canfd_frame frame;
    	memset(&frame, 0, sizeof(frame));
    	frame.can_id = 0;
	    frame.len = 1;
	    frame.data[0] = charge_time;
    	write(can, &frame, CAN_MTU); 
    } else if(cf->data[battery_pos] == 2) { //charging
    	power += 10;
    	if(power >= 100) {
    		power = 100;
    		times = 0;
    		charge = false;
    	}
        battery_green_rect.y -= 16;
        battery_green_rect.h += 16;
	    power_string = (char*)malloc(5 * sizeof(char)); 
		sprintf(power_string, "%d%%", power);
		TTF_SizeUTF8(font, power_string, 0, 0);
		font_surface = TTF_RenderUTF8_Solid(font, power_string, color);
		power_font_texture = SDL_CreateTextureFromSurface(renderer, font_surface);

		update_battery();
		SDL_RenderPresent(renderer);   
    }
}

void send_MSG_to_DB(struct CarData data){
    time_t current_time;
    struct tm *time_info;
    char time_str[25];
    char curl_command[400]; 

    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

    sprintf(curl_command, "curl -X POST -H \"Content-Type: application/json\" -d '{\"charge\":\"%d\", \"cost\":\"%d\", \"mileage\":\"%d\", \"speed\":\"%d\", \"driving_time\":\"%d\", \"inside_temp\":\"%d\", \"outside_temp\":\"%d\", \"battery_health\":\"%d\",\"time\":\"%s\"}' http://10.103.103.21:5000/api/car", power, data.cost, data.mileage, data.speed, data.driving_time, data.inside_temp, data.outside_temp, data.battery_health, time_str);
    system(curl_command);
    //printf("curl -X POST -H \"Content-Type: application/json\" -d '{\"charge\":\"%d\", \"cost\":\"%d\", \"mileage\":\"%d\", \"speed\":\"%d\", \"driving_time\":\"%d\", \"inside_temp\":\"%d\", \"outside_temp\":\"%d\", \"battery_health\":\"%d\", \"co2\":\"%d\",\"time\":\"%s\"}' http://10.103.103.21:5000/api/car", power, data.cost, data.mileage, data.car_speed, data.driving_time, data.inside_temp, data.outside_temp, data.battery_health, data.co2,time_str);
}

int main(int argc, char *argv[]) {
    struct ifreq ifr;
    struct sockaddr_can addr;
    struct canfd_frame frame;
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct timeval tv, timeout_config = {0, 0};
    fd_set rdfs;
    char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(sizeof(__u32))];
    int running = 1;
    int nbytes, maxdlen;
        
    TTF_Init();
    power_string = (char*)malloc(5 * sizeof(char)); 
    sprintf(power_string, "%d%%", power);
    font = TTF_OpenFont("./source/font.ttc", 30);
    TTF_SizeUTF8(font, power_string, 0, 0);

    SDL_Event event;

    
    if ((can = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) exit(1);

    addr.can_family = AF_CAN;
    memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strncpy(ifr.ifr_name, argv[optind], strlen(argv[optind]));
    //printf("Using CAN interface %s\n", ifr.ifr_name);
    if (ioctl(can, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        exit(1);
    }
    addr.can_ifindex = ifr.ifr_ifindex;
    // CAN FD Mode
    setsockopt(can, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));

    iov.iov_base = &frame;
    iov.iov_len = sizeof(frame);
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &ctrlmsg;
    msg.msg_controllen = sizeof(ctrlmsg);
    msg.msg_flags = 0;

    if (bind(can, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    init_car_state();

    SDL_Window *window = NULL;
    SDL_Surface *screenSurface = NULL;
    if(SDL_Init ( SDL_INIT_VIDEO ) < 0 ) {
        printf("SDL Could not initializes\n");
        exit(40);
    }
    window = SDL_CreateWindow("IC Simulator", 850, 300, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN); // | SDL_WINDOW_RESIZABLE);
    if(window == NULL) {
        printf("Window could not be shown\n");
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Surface *image = IMG_Load(get_data("ic.png"));
    SDL_Surface *needle = IMG_Load(get_data("needle.png"));
    SDL_Surface *sprites = IMG_Load(get_data("spritesheet.png"));
    base_texture = SDL_CreateTextureFromSurface(renderer, image);
    needle_tex = SDL_CreateTextureFromSurface(renderer, needle);
    sprite_tex = SDL_CreateTextureFromSurface(renderer, sprites);
    //font
    font_surface = TTF_RenderUTF8_Solid(font, power_string, color);
    power_font_texture = SDL_CreateTextureFromSurface(renderer, font_surface);
    //AC
    SDL_Surface *AC_black = IMG_Load(get_data("AC_black.png"));
    AC_black_tex = SDL_CreateTextureFromSurface(renderer, AC_black);
    SDL_Surface *AC_white = IMG_Load(get_data("AC_white.png"));
    AC_white_tex = SDL_CreateTextureFromSurface(renderer, AC_white);
    //battery
    SDL_Surface *battery_empty = IMG_Load(get_data("battery_empty.png"));
    battery_empty_tex = SDL_CreateTextureFromSurface(renderer, battery_empty);
    SDL_Surface *battery_green = IMG_Load(get_data("battery_green.png"));
    battery_green_tex = SDL_CreateTextureFromSurface(renderer, battery_green);
    //park
    SDL_Surface *park_red = IMG_Load(get_data("park_red.png"));
    park_red_tex = SDL_CreateTextureFromSurface(renderer, park_red);
    SDL_Surface *park_yellow = IMG_Load(get_data("park_yellow.png"));
    park_yellow_tex = SDL_CreateTextureFromSurface(renderer, park_yellow);
    SDL_Surface *park_white = IMG_Load(get_data("park_white.png"));
    park_white_tex = SDL_CreateTextureFromSurface(renderer, park_white);
    //seatbelt
    SDL_Surface *seatbelt_red = IMG_Load(get_data("seatbelt_red.png"));
    seatbelt_red_tex = SDL_CreateTextureFromSurface(renderer, seatbelt_red);
    SDL_Surface *seatbelt_white = IMG_Load(get_data("seatbelt_white.png"));
    seatbelt_white_tex = SDL_CreateTextureFromSurface(renderer, seatbelt_white);
    //brake
    SDL_Surface *brake_red = IMG_Load(get_data("brake_red.png"));
    brake_red_tex = SDL_CreateTextureFromSurface(renderer, brake_red);
    SDL_Surface *brake_yellow = IMG_Load(get_data("brake_yellow.png"));
    brake_yellow_tex = SDL_CreateTextureFromSurface(renderer, brake_yellow);
    SDL_Surface *brake_white = IMG_Load(get_data("brake_white.png"));
    brake_white_tex = SDL_CreateTextureFromSurface(renderer, brake_white);
    //degree
    SDL_Surface *degree = IMG_Load(get_data("degree.png"));
    degree_tex = SDL_CreateTextureFromSurface(renderer, degree);
    //logo
    SDL_Surface *logo = IMG_Load(get_data("ttu_logo.png"));
    logo_tex = SDL_CreateTextureFromSurface(renderer, logo);
    //set car data
    struct CarData carDataArray[15] = {
        {481, 120, 60, 2, 22, 18, 98, 1},
        {394, 90, 45, 4, 20, 21, 95, 2},
        {562, 150, 70, 1, 24, 27, 95, 3},
        {438, 110, 55, 3, 23, 20, 93, 2},
        {506, 130, 65, 2, 26, 28, 92, 4},
        {376, 80, 40, 2, 18, 16, 91, 1},
        {549, 140, 55, 8, 25, 23, 90, 1},
        {321, 70, 35, 2, 17, 15, 88, 4},
        {481, 120, 60, 3, 22, 18, 87, 1},
        {394, 90, 45, 7, 20, 21, 87, 3},
        {562, 150, 70, 2, 24, 27, 86, 2},
        {438, 110, 55, 5, 23, 20, 85, 1},
        {506, 130, 65, 6, 26, 28, 83, 3},
        {376, 80, 40, 8, 18, 16, 83, 4},
        {549, 140, 55, 4, 25, 23, 82, 2}
    };

    speed_rect.x = 212;
    speed_rect.y = 175;
    speed_rect.h = needle->h;
    speed_rect.w = needle->w;

    // Draw the IC
    redraw_ic();
    bool rec = true;
    

    while(running) {
        while( SDL_PollEvent(&event) != 0 ) {
            switch(event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_WINDOWEVENT:
                switch(event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                    redraw_ic();
                    break;
                }
            SDL_Delay(3);
        	}
    	}
        
        if(rec){
	        nbytes = recvmsg(can, &msg, 0);
	        if (nbytes < 0) {
	            perror("read");
	            return 1;
	        }
	        if ((size_t)nbytes == CAN_MTU)
	            maxdlen = CAN_MAX_DLEN;
	        else if ((size_t)nbytes == CANFD_MTU)
	            maxdlen = CANFD_MAX_DLEN;
	        else {
	            fprintf(stderr, "read: incomplete CAN frame\n");
	            return 1;
	        }
	        for (cmsg = CMSG_FIRSTHDR(&msg);
	                cmsg && (cmsg->cmsg_level == SOL_SOCKET);
	                cmsg = CMSG_NXTHDR(&msg,cmsg)) {
	            if (cmsg->cmsg_type == SO_TIMESTAMP)
	                tv = *(struct timeval *)CMSG_DATA(cmsg);
	            else if (cmsg->cmsg_type == SO_RXQ_OVFL)
	                //dropcnt[i] = *(__u32 *)CMSG_DATA(cmsg);
	                fprintf(stderr, "Dropped packet\n");
	        }

	        if(frame.can_id == DEFAULT_DOOR_ID) update_door_status(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_SIGNAL_ID) update_signal_status(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_AC_ID) update_AC_state(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_battery_ID) update_battery_state(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_brake_ID) update_brake_state(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_park_ID) update_park_state(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_save_ID) update_seatbelt_state(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_charge_ID) update_charge_state(&frame, maxdlen);
	        if(frame.can_id == DEFAULT_SPEED_ID) {update_speed_status(&frame, maxdlen);
	        	times++;
	        	if(times%100 == 0 && !charge){
	        		power-=2;
	        		if(power < 0) power = 0;
	        		battery_green_rect.y += 3;
        			battery_green_rect.h -= 3;
	      
	        		power_string = (char*)malloc(5 * sizeof(char)); 
				    sprintf(power_string, "%d%%", power);
				    TTF_SizeUTF8(font, power_string, 0, 0);
				    font_surface = TTF_RenderUTF8_Solid(font, power_string, color);
				    power_font_texture = SDL_CreateTextureFromSurface(renderer, font_surface);

				    update_battery();
				    SDL_RenderPresent(renderer);

                    if(times >= 500){
                        times = 0;

                        send_MSG_to_DB(carDataArray[CarData_index++]);
                        CarData_index %= 15;
                    }
	        	}
	        }

	        if(frame.can_id == 0){
	            rec = false;
	            charge = true;
	        }
    	}
    }
    free(power_string);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyTexture(battery_empty_tex);
    SDL_DestroyTexture(battery_green_tex);
    SDL_DestroyTexture(AC_black_tex);
    SDL_DestroyTexture(AC_white_tex);
    SDL_DestroyTexture(seatbelt_red_tex);
    SDL_DestroyTexture(brake_red_tex);
    SDL_DestroyTexture(brake_yellow_tex);
    SDL_DestroyTexture(park_red_tex);
    SDL_DestroyTexture(park_yellow_tex);
    SDL_DestroyTexture(base_texture);
    SDL_DestroyTexture(needle_tex);
    SDL_DestroyTexture(sprite_tex);
    SDL_DestroyTexture(power_font_texture);
    SDL_DestroyTexture(degree_tex);
    SDL_DestroyTexture(logo_tex);
    SDL_FreeSurface(font_surface);
    SDL_FreeSurface(battery_empty);
    SDL_FreeSurface(battery_green);
    SDL_FreeSurface(brake_red);
    SDL_FreeSurface(brake_yellow);
    SDL_FreeSurface(park_red);
    SDL_FreeSurface(park_yellow);
    SDL_FreeSurface(AC_black);
    SDL_FreeSurface(AC_white);
    SDL_FreeSurface(seatbelt_red);
    SDL_FreeSurface(image);
    SDL_FreeSurface(needle);
    SDL_FreeSurface(sprites);
    SDL_FreeSurface(degree);
    SDL_FreeSurface(logo);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();

    return 0;
}
