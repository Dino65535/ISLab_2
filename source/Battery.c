#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdbool.h>
#include "cJSON.h"

#ifndef DATA_DIR
#define DATA_DIR "./img/"
#endif

#define DEFAULT_battery_ID 700
#define DEFAULT_charge_ID 001
#define SCREEN_WIDTH 200
#define SCREEN_HEIGHT 150

#define BUF_SIZE 1024
#define SERVER_IP "10.103.103.60"
#define UDP_PORT 15120


int s; // socket
struct canfd_frame cf;
struct ifreq ifr;

int battery_pos = 0;
int battery_len = 1;
char battery_state = 1;
int battery_id = DEFAULT_battery_ID;

char charge_state = 1;
int charge_id = DEFAULT_charge_ID;
int charge_times = 0;

char data_file[256];

SDL_Renderer *renderer = NULL;
SDL_Texture *base_texture = NULL;

int socket_fd, TCP_PORT;
struct sockaddr_in server_addr;
char buffer[BUF_SIZE];
cJSON *session_ID_header;


char *get_data(char *fname) {
    if(strlen(DATA_DIR) + strlen(fname) > 255) return NULL;
    strncpy(data_file, DATA_DIR, 255);
    strncat(data_file, fname, 255-strlen(data_file));
    return data_file;
}

void send_pkt(int mtu) {
    if(write(s, &cf, mtu) != mtu) {
        perror("write");
    }
}

void send_battery_state(bool b) {
    if(b) {
        battery_state = 1;
    } else {
        battery_state = 0;
    }
    memset(&cf, 0, sizeof(cf));
    cf.can_id = battery_id;
    cf.len = battery_len;
    cf.data[battery_pos] = battery_state;
    send_pkt(CAN_MTU);
}

void sned_charge_state(bool b) {
    if(b) {
        charge_state = 1;
    } else {
        charge_state = 2;
    }
    memset(&cf, 0, sizeof(cf));
    cf.can_id = charge_id;
    cf.len = battery_len;
    cf.data[battery_pos] = charge_state;
    send_pkt(CAN_MTU);
}
//----------------------------
void Create_UDP() {
    if((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("無法建立UDP Socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(UDP_PORT);
}

void Create_TCP() {
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("無法建立TCP Socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(TCP_PORT);

    sleep(1);

    if(connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("無法連結到TCP server");
        exit(1);
    } else {
        printf("TCP建立成功\n");
        printf("--------------------\n");
    }
}

void TCP_Send_Message(cJSON *packet) {
    char *json_str = cJSON_Print(packet);

    //printf("TCP傳送 :%s\n", json_str);

    send(socket_fd, json_str, strlen(json_str), 0);
    cJSON_Delete(packet);
    free(json_str);
}

void TCP_Read_Message() {
    memset(buffer, 0, sizeof(buffer));
    read(socket_fd, buffer, BUF_SIZE);

    cJSON *response = cJSON_Parse(buffer);
    if(response == NULL) {
        perror("JSON解析失敗");
        exit(4);
    }

    char *json_str = cJSON_Print(response);
    //printf("TCP接收訊息:\n%s\n", json_str);
    //printf("--------------------\n");

    free(json_str);
    cJSON_Delete(response);
}

void Init_Stage() {
    //printf("Init_Stage :\n");

    cJSON *SDP = cJSON_CreateObject(), *Header = cJSON_CreateObject();
    cJSON_AddNumberToObject(Header, "POROTOCL_VERSION", 1);
    cJSON_AddNumberToObject(Header, "INV_POROTOCL_VERSION", 254);
    cJSON_AddNumberToObject(Header, "SDP_REQUEST", 36864);
    cJSON_AddItemToObject(SDP, "Header", Header);
    cJSON_AddNumberToObject(SDP, "Payload", 1000);

    char *json_str = cJSON_Print(SDP);

    if(sendto(socket_fd, json_str, strlen(json_str), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("UPD傳送訊息失敗");
        exit(2);
    }

    socklen_t serverLen = sizeof(server_addr);
    memset(buffer, 0, sizeof(buffer));
    if(recvfrom(socket_fd, buffer, BUF_SIZE, 0, (struct sockaddr*)&server_addr, &serverLen) < 0) {
        perror("UPD接收訊息失敗");
        exit(3);
    }

    cJSON *response = cJSON_Parse(buffer);
    if (response == NULL) {
        perror("SDP JSON解析失敗");
        exit(4);
    }
    memset(json_str, 0, sizeof(json_str));
    json_str = cJSON_Print(response);
    //printf("UDP接收訊息:\n%s\n", json_str);

    cJSON *portObject = cJSON_GetObjectItem(response, "Port");
    TCP_PORT = portObject->valueint;
    //printf("Port: %d\n", TCP_PORT);

    cJSON_Delete(SDP);
    cJSON_Delete(response);
    free(json_str);
    close(socket_fd);
    //printf("UDP關閉\n");
    //printf("--------------------\n");
}

void Supported_App_Protocol_Stage() {
    //printf("Supported_App_Protocol_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *supportedAppProtocolReq = cJSON_CreateObject();
    cJSON *AppProtocol_Array = cJSON_CreateArray();
    cJSON *AppProtocol = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "supportedAppProtocolReq", supportedAppProtocolReq);
    cJSON_AddItemToObject(supportedAppProtocolReq, "AppProtocol", AppProtocol_Array);
    cJSON_AddItemToArray(AppProtocol_Array, AppProtocol);

    cJSON_AddStringToObject(AppProtocol, "ProtocolNamespace", "urn:iso:15118:2:2013:MsgDef");
    cJSON_AddNumberToObject(AppProtocol, "VersionNumberMajor", 2);
    cJSON_AddNumberToObject(AppProtocol, "VersionNumberMinor", 0);
    cJSON_AddNumberToObject(AppProtocol, "SchemaID", 1);
    cJSON_AddNumberToObject(AppProtocol, "Priority", 1);

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Session_Setup_Stage() {
    //printf("Session_Setup_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_CreateObject();
    cJSON *Body = cJSON_CreateObject();
    cJSON *SessionSetupReq = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "SessionSetupReq", SessionSetupReq);

    cJSON_AddStringToObject(Header, "SessionID", "00");
    cJSON_AddStringToObject(SessionSetupReq, "EVCCID", "0242AC120003");

    TCP_Send_Message(packet);
    //這邊不呼叫TCP_Read_Message()，因為要讀取server給的sessionID
    //並建立一個Header含有sessionID的JSON，後面傳送的封包便不用重複建立
    /*
    *   "Header": {
    *       "SessionID": "443458DC402C19AA"
    *   }
    */
    memset(buffer, 0, sizeof(buffer));
    read(socket_fd, buffer, BUF_SIZE);

    cJSON *response = cJSON_Parse(buffer);
    if(response == NULL) {
        perror("JSON解析失敗");
        exit(4);
    }

    char *json_str = cJSON_Print(response);
    //printf("TCP接收訊息:\n%s\n", json_str);
    //printf("--------------------\n");

    char *session_ID = cJSON_GetObjectItem(response, "V2G_Message")->child->child->valuestring;
    cJSON_AddStringToObject(session_ID_header, "SessionID", session_ID);

    free(json_str);
    cJSON_Delete(response);
}

void Service_Discovery_Stage() {
    //printf("Service_Discovery_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();
    cJSON *ServiceDiscoveryReq = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "ServiceDiscoveryReq", ServiceDiscoveryReq);

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Payment_Service_Selection_Stage() {
    //printf("Payment_Service_Selection_Stag :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();
    cJSON *PaymentServiceSelectionReq = cJSON_CreateObject();
    cJSON *SelectedServiceList = cJSON_CreateObject();
    cJSON *SelectedService = cJSON_CreateArray();
    cJSON *serviceID = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "PaymentServiceSelectionReq", PaymentServiceSelectionReq);
    cJSON_AddStringToObject(PaymentServiceSelectionReq, "SelectedPaymentOption", "ExternalPayment");

    cJSON_AddItemToObject(PaymentServiceSelectionReq, "SelectedServiceList", SelectedServiceList);
    cJSON_AddItemToObject(SelectedServiceList, "SelectedService", SelectedService);
    cJSON_AddItemToArray(SelectedService, serviceID);
    cJSON_AddNumberToObject(serviceID, "ServiceID", 1);

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Authorization_Stage() {
    //printf("Authorization_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();
    cJSON *AuthorizationReq = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "AuthorizationReq", AuthorizationReq);

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Charge_Parameter_Discovery_Stage() {
    //printf("Charge_Parameter_Discovery_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();
    cJSON *chargeParameterDiscoveryReq = cJSON_CreateObject();
    cJSON *AC_EVChargeParameter = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "chargeParameterDiscoveryReq", chargeParameterDiscoveryReq);
    cJSON_AddStringToObject(chargeParameterDiscoveryReq, "RequestedEnergyTransferMode", "AC_three_phase_core");

    cJSON_AddItemToObject(chargeParameterDiscoveryReq, "AC_EVChargeParameter", AC_EVChargeParameter);
    cJSON_AddNumberToObject(AC_EVChargeParameter, "DepartureTime", 0);

    cJSON *EAmount = cJSON_CreateObject();
    cJSON *EVMaxVoltage = cJSON_CreateObject();
    cJSON *EVMaxCurrent = cJSON_CreateObject();
    cJSON *EVMinCurrent = cJSON_CreateObject();

    cJSON_AddItemToObject(AC_EVChargeParameter, "EAmount", EAmount);
    cJSON_AddItemToObject(AC_EVChargeParameter, "EVMaxVoltage", EVMaxVoltage);
    cJSON_AddItemToObject(AC_EVChargeParameter, "EVMaxCurrent", EVMaxCurrent);
    cJSON_AddItemToObject(AC_EVChargeParameter, "EVMinCurrent", EVMinCurrent);

    cJSON_AddNumberToObject(EAmount, "Value", 60);
    cJSON_AddNumberToObject(EAmount, "Multiplier", 0);
    cJSON_AddStringToObject(EAmount, "Unit", "Wh");

    cJSON_AddNumberToObject(EVMaxVoltage, "Value", 400);
    cJSON_AddNumberToObject(EVMaxVoltage, "Multiplier", 0);
    cJSON_AddStringToObject(EVMaxVoltage, "Unit", "V");

    cJSON_AddNumberToObject(EVMaxCurrent, "Value", 32000);
    cJSON_AddNumberToObject(EVMaxCurrent, "Multiplier", -3);
    cJSON_AddStringToObject(EVMaxCurrent, "Unit", "A");

    cJSON_AddNumberToObject(EVMinCurrent, "Value", 10);
    cJSON_AddNumberToObject(EVMinCurrent, "Multiplier", 0);
    cJSON_AddStringToObject(EVMinCurrent, "Unit", "A");

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Power_Delivery_Stage(bool b) {
    //printf("Power_Delivery_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);

    cJSON *PowerDeliveryReq = cJSON_CreateObject();

    cJSON_AddItemToObject(Body, "PowerDeliveryReq", PowerDeliveryReq);

    if(b) {
        cJSON_AddStringToObject(PowerDeliveryReq, "ChargeProgress", "Start");
        cJSON_AddNumberToObject(PowerDeliveryReq, "SAScheduleTupleID", 1);

        cJSON *ChargingProfile = cJSON_CreateObject();
        cJSON *ProfileEntry = cJSON_CreateArray();

        cJSON_AddItemToObject(PowerDeliveryReq, "ChargingProfile", ChargingProfile);
        cJSON_AddItemToObject(ChargingProfile, "ProfileEntry", ProfileEntry);

        cJSON *temp1 = cJSON_CreateObject();
        cJSON *ChargingProfileEntryMaxPower1 = cJSON_CreateObject();
        cJSON_AddNumberToObject(temp1, "ChargingProfileEntryStart", 0);
        cJSON_AddItemToObject(temp1, "ChargingProfileEntryMaxPower", ChargingProfileEntryMaxPower1);
        cJSON_AddNumberToObject(ChargingProfileEntryMaxPower1, "Value", 11000);
        cJSON_AddNumberToObject(ChargingProfileEntryMaxPower1, "Multiplier", 0);
        cJSON_AddStringToObject(ChargingProfileEntryMaxPower1, "Unit", "W");
        cJSON_AddItemToArray(ProfileEntry, temp1);

        cJSON *temp2 = cJSON_CreateObject();
        cJSON *ChargingProfileEntryMaxPower2 = cJSON_CreateObject();
        cJSON_AddNumberToObject(temp2, "ChargingProfileEntryStart", 43200);
        cJSON_AddItemToObject(temp2, "ChargingProfileEntryMaxPower", ChargingProfileEntryMaxPower2);
        cJSON_AddNumberToObject(ChargingProfileEntryMaxPower2, "Value", 7000);
        cJSON_AddNumberToObject(ChargingProfileEntryMaxPower2, "Multiplier", 0);
        cJSON_AddStringToObject(ChargingProfileEntryMaxPower2, "Unit", "W");
        cJSON_AddItemToArray(ProfileEntry, temp2);

        cJSON *temp3 = cJSON_CreateObject();
        cJSON *ChargingProfileEntryMaxPower3 = cJSON_CreateObject();
        cJSON_AddNumberToObject(temp3, "ChargingProfileEntryStart", 86400);
        cJSON_AddItemToObject(temp3, "ChargingProfileEntryMaxPower", ChargingProfileEntryMaxPower3);
        cJSON_AddNumberToObject(ChargingProfileEntryMaxPower3, "Value", 0);
        cJSON_AddNumberToObject(ChargingProfileEntryMaxPower3, "Multiplier", 0);
        cJSON_AddStringToObject(ChargingProfileEntryMaxPower3, "Unit", "W");
        cJSON_AddItemToArray(ProfileEntry, temp3);
    } else {
        cJSON_AddStringToObject(PowerDeliveryReq, "ChargeProgress", "Stop");
        cJSON_AddNumberToObject(PowerDeliveryReq, "SAScheduleTupleID", 1);
    }

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Charging_State() {
    //printf("Charging_State :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();
    cJSON *ChargingStatusReq = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "ChargingStatusReq", ChargingStatusReq);

    TCP_Send_Message(packet);
    TCP_Read_Message();
}

void Session_and_TCP_Server_Stop_Stage() {
    //printf("Session_and_TCP_Server_Stop_Stage :\n");

    cJSON *packet = cJSON_CreateObject();
    cJSON *V2G_Message = cJSON_CreateObject();
    cJSON *Header = cJSON_Duplicate(session_ID_header, 1);
    cJSON *Body = cJSON_CreateObject();
    cJSON *SessionStopReq = cJSON_CreateObject();

    cJSON_AddItemToObject(packet, "V2G_Message", V2G_Message);
    cJSON_AddItemToObject(V2G_Message, "Header", Header);
    cJSON_AddItemToObject(V2G_Message, "Body", Body);
    cJSON_AddItemToObject(Body, "SessionStopReq", SessionStopReq);
    cJSON_AddStringToObject(SessionStopReq, "ChargingSession", "Terminate");

    TCP_Send_Message(packet);
    TCP_Read_Message();

    if (close(socket_fd) < 0) {
        perror("伺服器關閉失敗");
    } else {
        printf("TCP關閉");
    }
}

void communicate_with_charging_pile() {
    session_ID_header = cJSON_CreateObject();

    Create_UDP();
    Init_Stage();

    Create_TCP();

    Supported_App_Protocol_Stage();
    sleep(1);

    Session_Setup_Stage();
    sleep(1);

    Service_Discovery_Stage();
    sleep(1);

    Payment_Service_Selection_Stage();
    sleep(1);

    Authorization_Stage();
    sleep(1);

    Charge_Parameter_Discovery_Stage();
    sleep(1);

    Power_Delivery_Stage(true); //true >> ChargeProgress : Start
    sleep(1);

    for(int i = 1; i<=charge_times; i++) {
        Charging_State();
        sned_charge_state(0);
        printf("充電%d次\n", i);
        sleep(1);
    }
    printf("充電結束\n");
    printf("--------------------\n");

    Power_Delivery_Stage(false); //false >> ChargeProgress : Stop
    sleep(1);

    Session_and_TCP_Server_Stop_Stage();
}
//----------------------------
void redraw_screen() {
    SDL_RenderCopy(renderer, base_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int main(int argc, char *argv[]) {
    struct sockaddr_can addr;
    int running = 1;
    int enable_canfd = 1;
    SDL_Event event;

    /* open socket */
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket");
        return 1;
    }

    addr.can_family = AF_CAN;

    strcpy(ifr.ifr_name, argv[optind]);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        return 1;
    }
    addr.can_ifindex = ifr.ifr_ifindex;

    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                   &enable_canfd, sizeof(enable_canfd))) {
        printf("error when enabling CAN FD support\n");
        return 1;
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    // GUI Setup
    SDL_Window *window = NULL;
    SDL_Surface *screenSurface = NULL;
    
    window = SDL_CreateWindow("Battery", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if(window == NULL) {
        printf("Window could not be shown\n");
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Surface *image = IMG_Load(get_data("battery_ECU.png"));
    base_texture = SDL_CreateTextureFromSurface(renderer, image);
    SDL_RenderCopy(renderer, base_texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    while(running) {
        while( SDL_PollEvent(&event) != 0 ) {
            switch(event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_WINDOWEVENT:
                switch(event.window.event) {
                case SDL_WINDOWEVENT_ENTER:
                case SDL_WINDOWEVENT_RESIZED:
                    redraw_screen();
                    break;
                }
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym) {
                case SDLK_a:
                    send_battery_state(1);
                    break;
                case SDLK_s:
                    send_battery_state(0);
                    break;
                case SDLK_d:
                    sned_charge_state(1);
                    while(read(s, &cf, sizeof(struct canfd_frame))) {
                        if(cf.can_id == 000) {
                            charge_times = cf.data[0];
                            break;
                        }
                    }
                    communicate_with_charging_pile();
                    break;
                }
            }
        }
        SDL_Delay(5);
    }
    close(s);
    SDL_DestroyTexture(base_texture);
    SDL_FreeSurface(image);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
