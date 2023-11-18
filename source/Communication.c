#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include "cJSON.h"

int server_socket, client_socket;
void signal_handler(int signum) {
    if (signum == SIGINT) {
        close(server_socket);
        close(client_socket);
        exit(0);
    }
}

int main(int argc, char const *argv[])
{
    signal(SIGINT, signal_handler);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }    

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5000);
    //server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket binding failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) == -1) {
        perror("Listening failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Accepting failed");
            continue;
        }

        char buffer[1024];
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received == -1) {
            perror("Receiving data failed");
        } else {
            char *jsonStart = strstr(buffer, "{");
            cJSON *json = cJSON_Parse(jsonStart);
            if (json == NULL) {
                printf("JSON parsing failed\n");
                continue;
            }

            cJSON *currentItem = json->child;
            while (currentItem) {
                if (currentItem->string != NULL) {
                    //printf("Key: %s, Value: %s\n", currentItem->string, currentItem->valuestring);
                    if(strcmp(currentItem->string, "156748") == 0) {
                        if(strcmp(currentItem->valuestring, "DOOR_OPEN") == 0) {
                            system("cansend vcan0 19B#00000E");
                            char *response_message = "接收到開門訊息\n";
                            send(client_socket, response_message, strlen(response_message), 0);
                        }
                        else if(strcmp(currentItem->valuestring, "DOOR_CLOSE") == 0) {
                            system("cansend vcan0 19B#00000F");
                            char *response_message = "接收到關門訊息\n";
                            send(client_socket, response_message, strlen(response_message), 0);
                        }
                        else if(strcmp(currentItem->valuestring, "AC_OPEN") == 0) {
                            system("cansend vcan0 320#000001");
                            char *response_message = "接收到開冷氣訊息\n";
                            send(client_socket, response_message, strlen(response_message), 0);
                        }
                        else if(strcmp(currentItem->valuestring, "AC_CLOSE") == 0) {
                            system("cansend vcan0 320#000000");
                            char *response_message = "接收到關冷氣訊息\n";
                            send(client_socket, response_message, strlen(response_message), 0);
                        }
                    }
                }
                currentItem = currentItem->next;
            }
        }

        close(client_socket);
    }
    close(server_socket);

	return 0;
}

