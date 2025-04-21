#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 21
#define SERVER_IP "127.0.0.1"

// Global variables
int control_fd;
int data_port = 0;
int authenticated = 0;

// Function prototypes
void send_command(char *command);
void receive_response(char *response, int size);
void handle_user_command(char *username);
void handle_pass_command(char *password);
void handle_list_command();
void handle_local_list_command();
void handle_cwd_command(char *dir);
void handle_local_cwd_command(char *dir);
void handle_pwd_command();
void handle_local_pwd_command();
void handle_retr_command(char *filename);
void handle_stor_command(char *filename);
void handle_quit_command();
int setup_data_connection();
void display_welcome_message();

int main() {
    struct sockaddr_in server_addr;
    
    // Create control socket
    if ((control_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Connect to server
    if (connect(control_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(control_fd);
        exit(EXIT_FAILURE);
    }
    
    // Display welcome message
    display_welcome_message();
    
    // Receive server greeting
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    // Main command loop
    char command[BUFFER_SIZE];
    while (1) {
        printf("ftp> ");
        fflush(stdout);
        
        if (fgets(command, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = '\0';
        
        if (strlen(command) == 0) {
            continue;
        }
        
        // Extract command and argument
        char cmd[BUFFER_SIZE];
        char arg[BUFFER_SIZE];
        
        if (sscanf(command, "%s %s", cmd, arg) < 1) {
            continue;
        }
        
        // Handle commands
        if (strcmp(cmd, "USER") == 0) {
            handle_user_command(arg);
        } else if (strcmp(cmd, "PASS") == 0) {
            handle_pass_command(arg);
        } else if (strcmp(cmd, "LIST") == 0) {
            handle_list_command();
        } else if (strcmp(cmd, "!LIST") == 0) {
            handle_local_list_command();
        } else if (strcmp(cmd, "CWD") == 0) {
            handle_cwd_command(arg);
        } else if (strcmp(cmd, "!CWD") == 0) {
            handle_local_cwd_command(arg);
        } else if (strcmp(cmd, "PWD") == 0) {
            handle_pwd_command();
        } else if (strcmp(cmd, "!PWD") == 0) {
            handle_local_pwd_command();
        } else if (strcmp(cmd, "RETR") == 0) {
            handle_retr_command(arg);
        } else if (strcmp(cmd, "STOR") == 0) {
            handle_stor_command(arg);
        } else if (strcmp(cmd, "QUIT") == 0) {
            handle_quit_command();
            break;
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }
    
    return 0;
}

void display_welcome_message() {
    printf("Hello!! Please Authenticate\n");
    printf("1. type \"USER\" followed by a space and your username\n");
    printf("2. type \"PASS\" followed by a space and your password\n");
    printf("or type \"QUIT\" to close connection at any moment\n\n");
    printf("Once Authenticated this is the list of commands:\n");
    printf("\"STOR\" + space + filename | to send a file to the server\n");
    printf("\"RETR\" + space + filename | to download a file from the server\n");
    printf("\"LIST\" | to list all the files under the current server directory\n");
    printf("\"CWD\" + space + directory | to change the current server directory\n");
    printf("\"PWD\" | to display the current server directory\n");
    printf("Add \"!\" before the last three commands to apply them locally\n");
}

void send_command(char *command) {
    char full_command[BUFFER_SIZE];
    sprintf(full_command, "%s\r\n", command);
    write(control_fd, full_command, strlen(full_command));
}

void receive_response(char *response, int size) {
    memset(response, 0, size);
    read(control_fd, response, size - 1);
    
    // Remove trailing newline
    response[strcspn(response, "\r\n")] = '\0';
}

int setup_data_connection() {
    struct sockaddr_in data_addr;
    socklen_t addr_len = sizeof(data_addr);
    int data_listen_fd;
    
    // Create data socket
    if ((data_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Data socket creation failed");
        return -1;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(data_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_listen_fd);
        return -1;
    }
    
    // Initialize data address
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0;  // Let the system assign a port
    
    // Bind data socket
    if (bind(data_listen_fd, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("Data bind failed");
        close(data_listen_fd);
        return -1;
    }
    
    // Get assigned port
    if (getsockname(data_listen_fd, (struct sockaddr *)&data_addr, &addr_len) < 0) {
        perror("Getsockname failed");
        close(data_listen_fd);
        return -1;
    }
    
    data_port = ntohs(data_addr.sin_port);
    
    // Listen for connections
    if (listen(data_listen_fd, 1) < 0) {
        perror("Data listen failed");
        close(data_listen_fd);
        return -1;
    }
    
    // Send PORT command to server
    char port_command[BUFFER_SIZE];
    sprintf(port_command, "PORT 127,0,0,1,%d,%d", data_port / 256, data_port % 256);
    send_command(port_command);
    
    // Receive response
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    // Wait for server to connect
    struct sockaddr_in server_data_addr;
    socklen_t server_addr_len = sizeof(server_data_addr);
    
    int data_fd = accept(data_listen_fd, (struct sockaddr *)&server_data_addr, &server_addr_len);
    if (data_fd < 0) {
        perror("Data accept failed");
        close(data_listen_fd);
        return -1;
    }
    
    // Close listen socket
    close(data_listen_fd);
    
    // Return data socket
    return data_fd;
}

void handle_user_command(char *username) {
    char command[BUFFER_SIZE];
    sprintf(command, "USER %s", username);
    send_command(command);
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
}

void handle_pass_command(char *password) {
    char command[BUFFER_SIZE];
    sprintf(command, "PASS %s", password);
    send_command(command);
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    if (strncmp(response, "230", 3) == 0) {
        authenticated = 1;
    }
}

void handle_list_command() {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    
    // Setup data connection
    int data_fd = setup_data_connection();
    if (data_fd < 0) {
        return;
    }
    
    // Send LIST command
    send_command("LIST");
    
    // Wait for the incoming connection and process the data first
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    while ((bytes_read = read(data_fd, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    
    // Close data connection
    close(data_fd);
    
    // Now read the responses - both the "150" and "226" messages
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
}

void handle_local_list_command() {
    // Execute shell command
    system("ls");
}

void handle_cwd_command(char *dir) {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    
    char command[BUFFER_SIZE];
    sprintf(command, "CWD %s", dir);
    send_command(command);
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
}

void handle_local_cwd_command(char *dir) {
    if (chdir(dir) != 0) {
        perror("Local CWD failed");
    }
}

void handle_pwd_command() {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    
    send_command("PWD");
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
}

void handle_local_pwd_command() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, BUFFER_SIZE) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("Local PWD failed");
    }
}

void handle_retr_command(char *filename) {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    
    // Setup data connection
    int data_fd = setup_data_connection();
    if (data_fd < 0) {
        return;
    }
    
    // Send RETR command
    char command[BUFFER_SIZE];
    sprintf(command, "RETR %s", filename);
    send_command(command);
    
    // Receive response
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    if (strncmp(response, "150", 3) == 0) {
        // Open file for writing
        FILE *fp = fopen(filename, "wb");
        if (fp == NULL) {
            perror("File open failed");
            close(data_fd);
            return;
        }
        
        // Receive file
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;
        
        while ((bytes_read = read(data_fd, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, 1, bytes_read, fp);
        }
        
        fclose(fp);
        
        // Close data connection
        close(data_fd);
        
        // Receive final response
        receive_response(response, BUFFER_SIZE);
        printf("%s\n", response);
    }
}

void handle_stor_command(char *filename) {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    
    // Check if file exists
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("File not found");
        return;
    }
    fclose(fp);
    
    // Setup data connection
    int data_fd = setup_data_connection();
    if (data_fd < 0) {
        return;
    }
    
    // Send STOR command
    char command[BUFFER_SIZE];
    sprintf(command, "STOR %s", filename);
    send_command(command);
    
    // Receive response
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    if (strncmp(response, "150", 3) == 0) {
        // Open file for reading
        fp = fopen(filename, "rb");
        if (fp == NULL) {
            perror("File open failed");
            close(data_fd);
            return;
        }
        
        // Send file
        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            write(data_fd, buffer, bytes_read);
        }
        
        fclose(fp);
        
        // Close data connection
        close(data_fd);
        
        // Receive final response
        receive_response(response, BUFFER_SIZE);
        printf("%s\n", response);
    }
}

void handle_quit_command() {
    send_command("QUIT");
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    close(control_fd);
}