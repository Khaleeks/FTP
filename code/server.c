#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MAX_USERS 50
#define MAX_USERNAME 50
#define MAX_PASSWORD 50
#define SERVER_PORT 21
#define MAX_PENDING 5

// User structure for authentication
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} User;

// FTP Session structure
typedef struct {
    int control_fd;
    int data_fd;
    char username[MAX_USERNAME];
    int authenticated;
    char current_dir[BUFFER_SIZE];
    char root_dir[BUFFER_SIZE];
    char data_ip[50];    // Added for storing client's IP for data connection
    int data_port;       // Added for storing client's port for data connection
} FTPSession;

// Global variables
User users[MAX_USERS];
int num_users = 0;
FTPSession client_sessions[MAX_CLIENTS];

// Function prototypes
void initialize_sessions();
void handle_client(int client_fd);
void handle_command(int client_fd, char *command);
void send_response(int client_fd, char *response);
int authenticate_user(char *username, char *password);
void load_users();
void handle_user_command(int client_fd, char *username);
void handle_pass_command(int client_fd, char *password);
void handle_quit_command(int client_fd);
void handle_port_command(int client_fd, char *port_args);
void handle_list_command(int client_fd);
void handle_cwd_command(int client_fd, char *dir);
void handle_pwd_command(int client_fd);
void handle_retr_command(int client_fd, char *filename);
void handle_stor_command(int client_fd, char *filename);
void handle_mkd_command(int client_fd, char *dirname);
void handle_rmd_command(int client_fd, char *dirname);
void handle_dele_command(int client_fd, char *filename);
void handle_rnfr_command(int client_fd, char *oldname);
void handle_rnto_command(int client_fd, char *newname);
int get_session_index(int client_fd);
void clean_up_session(int session_index);
void handle_child_process(int sig);

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    fd_set read_fds, master_fds;
    int max_fd, i;
    
    // Initialize all sessions
    initialize_sessions();
    
    // Load users from CSV file
    load_users();
    
    // Handle child processes to avoid zombies
    signal(SIGCHLD, handle_child_process);
    
    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, MAX_PENDING) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server started on port %d\n", SERVER_PORT);
    
    // Initialize fd sets
    FD_ZERO(&master_fds);
    FD_SET(server_fd, &master_fds);
    max_fd = server_fd;
    
    // Main loop
    while (1) {
        read_fds = master_fds;
        
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR)
                continue;
            perror("Select failed");
            exit(EXIT_FAILURE);
        }
        
        // Check for new connections
        if (FD_ISSET(server_fd, &read_fds)) {
            client_len = sizeof(client_addr);
            if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
                perror("Accept failed");
                continue;
            }
            
            FD_SET(client_fd, &master_fds);
            if (client_fd > max_fd)
                max_fd = client_fd;
            
            // Initialize new client session
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sessions[i].control_fd == -1) {
                    client_sessions[i].control_fd = client_fd;
                    client_sessions[i].authenticated = 0;
                    client_sessions[i].data_fd = -1;  // Initialize data_fd
                    client_sessions[i].data_port = -1; // Initialize data_port
                    memset(client_sessions[i].data_ip, 0, sizeof(client_sessions[i].data_ip)); // Initialize data_ip
                    // Set current and root directory
                    char cwd[BUFFER_SIZE];
                    if (getcwd(cwd, BUFFER_SIZE) != NULL) {
                        sprintf(client_sessions[i].root_dir, "%s", cwd);
                        sprintf(client_sessions[i].current_dir, "%s", cwd);
                    }
                    break;
                }
            }
            
            printf("Connection established with user %d\n", i);
            printf("Their port: %d\n", ntohs(client_addr.sin_port));
            
            // Send welcome message
            send_response(client_fd, "220 Service ready for new user.");
        }
        
        // Check for client data
        for (i = 0; i <= max_fd; i++) {
            if (i != server_fd && FD_ISSET(i, &read_fds)) {
                char buffer[BUFFER_SIZE];
                int bytes_read = read(i, buffer, BUFFER_SIZE - 1);
                
                if (bytes_read <= 0) {
                    // Client disconnected
                    int session_index = get_session_index(i);
                    if (session_index != -1) {
                        clean_up_session(session_index);
                    }
                    close(i);
                    FD_CLR(i, &master_fds);
                } else {
                    // Handle command
                    buffer[bytes_read] = '\0';
                    // Remove newline characters
                    for (int j = 0; j < bytes_read; j++) {
                        if (buffer[j] == '\n' || buffer[j] == '\r') {
                            buffer[j] = '\0';
                        }
                    }
                    handle_command(i, buffer);
                }
            }
        }
    }
    
    close(server_fd);
    return 0;
}

void initialize_sessions() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sessions[i].control_fd = -1;
        client_sessions[i].data_fd = -1;
        client_sessions[i].data_port = -1;
        client_sessions[i].authenticated = 0;
        memset(client_sessions[i].username, 0, MAX_USERNAME);
        memset(client_sessions[i].current_dir, 0, BUFFER_SIZE);
        memset(client_sessions[i].root_dir, 0, BUFFER_SIZE);
        memset(client_sessions[i].data_ip, 0, sizeof(client_sessions[i].data_ip));
    }
}

void handle_child_process(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void load_users() {
    FILE *fp = fopen("../users.csv", "r");
    if (fp == NULL) {
        perror("Failed to open users.csv");
        exit(EXIT_FAILURE);
    }
    
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        char *username = strtok(line, ",");
        char *password = strtok(NULL, ",\n");
        
        if (username && password) {
            strncpy(users[num_users].username, username, MAX_USERNAME);
            strncpy(users[num_users].password, password, MAX_PASSWORD);
            num_users++;
        }
    }
    
    fclose(fp);
}

int get_session_index(int client_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sessions[i].control_fd == client_fd) {
            return i;
        }
    }
    return -1;
}

void clean_up_session(int session_index) {
    if (client_sessions[session_index].data_fd != -1) {
        close(client_sessions[session_index].data_fd);
    }
    
    client_sessions[session_index].control_fd = -1;
    client_sessions[session_index].data_fd = -1;
    client_sessions[session_index].data_port = -1;
    client_sessions[session_index].authenticated = 0;
    memset(client_sessions[session_index].username, 0, MAX_USERNAME);
    memset(client_sessions[session_index].data_ip, 0, sizeof(client_sessions[session_index].data_ip));
}

void send_response(int client_fd, char *response) {
    char full_response[BUFFER_SIZE];
    sprintf(full_response, "%s\r\n", response);
    write(client_fd, full_response, strlen(full_response));
}

int authenticate_user(char *username, char *password) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].username, username) == 0 && 
            strcmp(users[i].password, password) == 0) {
            return 1;
        }
    }
    return 0;
}

void handle_command(int client_fd, char *command) {
    char cmd[BUFFER_SIZE];
    char arg[BUFFER_SIZE];
    
    // Parse command and arguments
    if (sscanf(command, "%s %s", cmd, arg) < 1) {
        send_response(client_fd, "500 Syntax error, command unrecognized.");
        return;
    }
    
    int session_index = get_session_index(client_fd);
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    // Handle commands
    if (strcmp(cmd, "USER") == 0) {
        handle_user_command(client_fd, arg);
    } else if (strcmp(cmd, "PASS") == 0) {
        handle_pass_command(client_fd, arg);
    } else if (strcmp(cmd, "QUIT") == 0) {
        handle_quit_command(client_fd);
    } else if (!client_sessions[session_index].authenticated) {
        send_response(client_fd, "530 Not logged in.");
    } else if (strcmp(cmd, "PORT") == 0) {
        handle_port_command(client_fd, arg);
    } else if (strcmp(cmd, "LIST") == 0) {
        handle_list_command(client_fd);
    } else if (strcmp(cmd, "CWD") == 0) {
        handle_cwd_command(client_fd, arg);
    } else if (strcmp(cmd, "PWD") == 0) {
        handle_pwd_command(client_fd);
    } else if (strcmp(cmd, "RETR") == 0) {
        handle_retr_command(client_fd, arg);
    } else if (strcmp(cmd, "STOR") == 0) {
        handle_stor_command(client_fd, arg);
    } else if (strcmp(cmd, "MKD") == 0) {
        handle_mkd_command(client_fd, arg);
    } else if (strcmp(cmd, "RMD") == 0) {
        handle_rmd_command(client_fd, arg);
    } else if (strcmp(cmd, "DELE") == 0) {
        handle_dele_command(client_fd, arg);
    } else if (strcmp(cmd, "RNFR") == 0) {
        handle_rnfr_command(client_fd, arg);
    } else if (strcmp(cmd, "RNTO") == 0) {
        handle_rnto_command(client_fd, arg);
    } else {
        send_response(client_fd, "202 Command not implemented.");
    }
}

void handle_user_command(int client_fd, char *username) {
    int session_index = get_session_index(client_fd);
    
    if (session_index != -1) {
        // Check if username exists
        int user_exists = 0;
        for (int i = 0; i < num_users; i++) {
            if (strcmp(users[i].username, username) == 0) {
                user_exists = 1;
                break;
            }
        }
        
        if (user_exists) {
            strncpy(client_sessions[session_index].username, username, MAX_USERNAME);
            printf("Successful username verification\n");
            send_response(client_fd, "331 Username OK, need password.");
        } else {
            send_response(client_fd, "530 Not logged in.");
        }
    } else {
        send_response(client_fd, "500 Internal server error.");
    }
}

void handle_pass_command(int client_fd, char *password) {
    int session_index = get_session_index(client_fd);
    
    if (session_index != -1 && client_sessions[session_index].username[0] != '\0') {
        if (authenticate_user(client_sessions[session_index].username, password)) {
            client_sessions[session_index].authenticated = 1;
            
            // Create user directory if it doesn't exist
            char user_dir[BUFFER_SIZE];
            sprintf(user_dir, "%s/%s", client_sessions[session_index].root_dir, client_sessions[session_index].username);
            mkdir(user_dir, 0777);
            
            // Set current directory to user directory
            sprintf(client_sessions[session_index].current_dir, "%s", user_dir);
            
            printf("Successful login\n");
            send_response(client_fd, "230 User logged in, proceed.");
        } else {
            send_response(client_fd, "530 Not logged in.");
        }
    } else {
        send_response(client_fd, "503 Bad sequence of commands.");
    }
}

void handle_quit_command(int client_fd) {
    send_response(client_fd, "221 Service closing control connection.");
    
    int session_index = get_session_index(client_fd);
    if (session_index != -1) {
        clean_up_session(session_index);
    }
    
    close(client_fd);
}

void handle_port_command(int client_fd, char *port_args) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    printf("Port received: %s\n", port_args);
    
    // Parse PORT arguments
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(port_args, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        send_response(client_fd, "501 Syntax error in parameters.");
        return;
    }
    
    // Store client data port and IP in session
    sprintf(client_sessions[session_index].data_ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    client_sessions[session_index].data_port = p1 * 256 + p2;
    
    send_response(client_fd, "200 PORT command successful.");
}

void handle_list_command(int client_fd) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1 || client_sessions[session_index].data_port == -1) {
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Get client IP and port
    char client_ip[50];
    strcpy(client_ip, client_sessions[session_index].data_ip);
    int client_port = client_sessions[session_index].data_port;
    
    // Create data socket
    int data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_fd < 0) {
        perror("Socket creation failed");
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(data_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Initialize client data address
    struct sockaddr_in client_data_addr;
    memset(&client_data_addr, 0, sizeof(client_data_addr));
    client_data_addr.sin_family = AF_INET;
    client_data_addr.sin_addr.s_addr = inet_addr(client_ip);
    client_data_addr.sin_port = htons(client_port);
    
    send_response(client_fd, "150 File status okay; about to open data connection.");
    printf("File okay, beginning data connections\n");
    printf("Connecting to Client Transfer Socket...\n");
    
    // Connect to client data port
    if (connect(data_fd, (struct sockaddr *)&client_data_addr, sizeof(client_data_addr)) < 0) {
        perror("Connect failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    printf("Connection Successful\n");
    printf("Listing directory\n");
    
    // Fork to handle data transfer
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        close(data_fd);
        send_response(client_fd, "451 Requested action aborted: local error in processing.");
        return;
    }
    
    if (pid == 0) {
        // Child process
        close(client_fd);
        
        // List directory contents
        DIR *dir;
        struct dirent *ent;
        
        if ((dir = opendir(client_sessions[session_index].current_dir)) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                // Skip . and ..
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                    continue;
                }
                
                char file_info[BUFFER_SIZE];
                sprintf(file_info, "%s\r\n", ent->d_name);
                write(data_fd, file_info, strlen(file_info));
            }
            closedir(dir);
        }
        
        close(data_fd);
        exit(0);
    } else {
        // Parent process
        close(data_fd);
        
        // Reset data connection info
        client_sessions[session_index].data_port = -1;
        memset(client_sessions[session_index].data_ip, 0, sizeof(client_sessions[session_index].data_ip));
        
        // Wait for child to finish
        waitpid(pid, NULL, 0);
        
        printf("226 Transfer complete\n");
        send_response(client_fd, "226 Transfer complete.");
    }
}

void handle_cwd_command(int client_fd, char *dir) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    char new_dir[BUFFER_SIZE];
    
    // Check if directory is absolute or relative
    if (dir[0] == '/') {
        sprintf(new_dir, "%s", dir);
    } else {
        sprintf(new_dir, "%s/%s", client_sessions[session_index].current_dir, dir);
    }
    
    // Check if directory exists
    DIR *dir_ptr = opendir(new_dir);
    if (dir_ptr) {
        closedir(dir_ptr);
        sprintf(client_sessions[session_index].current_dir, "%s", new_dir);
        
        printf("Changing directory to: %s\n", dir);
        
        // Format response
        char response[BUFFER_SIZE];
        // Get relative path from root to current directory
        char rel_path[BUFFER_SIZE];
        strcpy(rel_path, client_sessions[session_index].current_dir);
        
        // Remove root_dir from path to get relative path
        char *username_pos = strstr(rel_path, client_sessions[session_index].username);
        if (username_pos) {
            sprintf(response, "200 directory changed to %s/.", username_pos);
        } else {
            sprintf(response, "200 directory changed to %s/.", rel_path);
        }
        
        send_response(client_fd, response);
    } else {
        send_response(client_fd, "550 No such file or directory.");
    }
}

void handle_pwd_command(int client_fd) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    char response[BUFFER_SIZE];
    
    // Get relative path from root to current directory
    char rel_path[BUFFER_SIZE];
    strcpy(rel_path, client_sessions[session_index].current_dir);
    
    // Remove root_dir from path to get relative path
    char *username_pos = strstr(rel_path, client_sessions[session_index].username);
    if (username_pos) {
        sprintf(response, "257 %s/", username_pos);
    } else {
        sprintf(response, "257 %s/", rel_path);
    }
    
    send_response(client_fd, response);
}

void handle_retr_command(int client_fd, char *filename) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1 || client_sessions[session_index].data_port == -1) {
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Get client IP and port
    char client_ip[50];
    strcpy(client_ip, client_sessions[session_index].data_ip);
    int client_port = client_sessions[session_index].data_port;
    
    // Create data socket
    int data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_fd < 0) {
        perror("Socket creation failed");
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(data_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Check if file exists
    char filepath[BUFFER_SIZE];
    sprintf(filepath, "%s/%s", client_sessions[session_index].current_dir, filename);
    
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        send_response(client_fd, "550 No such file or directory.");
        close(data_fd);
        return;
    }
    
    fclose(fp);
    
    // Initialize client data address
    struct sockaddr_in client_data_addr;
    memset(&client_data_addr, 0, sizeof(client_data_addr));
    client_data_addr.sin_family = AF_INET;
    client_data_addr.sin_addr.s_addr = inet_addr(client_ip);
    client_data_addr.sin_port = htons(client_port);
    
    send_response(client_fd, "150 File status okay; about to open data connection.");
    printf("File okay, beginning data connections\n");
    printf("Connecting to Client Transfer Socket...\n");
    
    // Connect to client data port
    if (connect(data_fd, (struct sockaddr *)&client_data_addr, sizeof(client_data_addr)) < 0) {
        perror("Connect failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    printf("Connection Successful\n");
    
    // Fork to handle data transfer
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        close(data_fd);
        send_response(client_fd, "451 Requested action aborted: local error in processing.");
        return;
    }
    
    if (pid == 0) {
        // Child process
        close(client_fd);
        
        // Open file for reading
        fp = fopen(filepath, "rb");
        if (fp == NULL) {
            close(data_fd);
            exit(1);
        }
        
        // Transfer file
        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            write(data_fd, buffer, bytes_read);
        }
        
        fclose(fp);
        close(data_fd);
        exit(0);
    } else {
        // Parent process
        close(data_fd);
        
        // Reset data connection info
        client_sessions[session_index].data_port = -1;
        memset(client_sessions[session_index].data_ip, 0, sizeof(client_sessions[session_index].data_ip));
        
        // Wait for child to finish
        waitpid(pid, NULL, 0);
        
        printf("226 Transfer complete\n");
        send_response(client_fd, "226 Transfer complete.");
    }
}

void handle_stor_command(int client_fd, char *filename) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1 || client_sessions[session_index].data_port == -1) {
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Get client IP and port
    char client_ip[50];
    strcpy(client_ip, client_sessions[session_index].data_ip);
    int client_port = client_sessions[session_index].data_port;
    
    // Create data socket
    int data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_fd < 0) {
        perror("Socket creation failed");
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(data_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Create temporary filepath
    char temp_filepath[BUFFER_SIZE];
    sprintf(temp_filepath, "%s/tmp_%ld_%s", client_sessions[session_index].current_dir, (long)time(NULL), filename);
    
    // Create final filepath
    char final_filepath[BUFFER_SIZE];
    sprintf(final_filepath, "%s/%s", client_sessions[session_index].current_dir, filename);
    
    // Initialize client data address
    struct sockaddr_in client_data_addr;
    memset(&client_data_addr, 0, sizeof(client_data_addr));
    client_data_addr.sin_family = AF_INET;
    client_data_addr.sin_addr.s_addr = inet_addr(client_ip);
    client_data_addr.sin_port = htons(client_port);
    
    send_response(client_fd, "150 File status okay; about to open data connection.");
    printf("File okay, beginning data connections\n");
    printf("Connecting to Client Transfer Socket...\n");
    
    // Connect to client data port
    if (connect(data_fd, (struct sockaddr *)&client_data_addr, sizeof(client_data_addr)) < 0) {
        perror("Connect failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    printf("Connection Successful\n");
    
    // Fork to handle data transfer
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        close(data_fd);
        send_response(client_fd, "451 Requested action aborted: local error in processing.");
        return;
    }
    
    if (pid == 0) {
        // Child process
        close(client_fd);
        
        // Open temporary file for writing
        FILE *fp = fopen(temp_filepath, "wb");
        if (fp == NULL) {
            close(data_fd);
            exit(1);
        }
        
        // Receive file
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;
        
        while ((bytes_read = read(data_fd, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, 1, bytes_read, fp);
        }
        
        fclose(fp);
        close(data_fd);
        
        // Rename temporary file to final filename
        rename(temp_filepath, final_filepath);
        
        exit(0);
    } else {
        // Parent process
        close(data_fd);
        
        // Reset data connection info
        client_sessions[session_index].data_port = -1;
        memset(client_sessions[session_index].data_ip, 0, sizeof(client_sessions[session_index].data_ip));
        
        // Wait for child to finish
        waitpid(pid, NULL, 0);
        
        printf("226 Transfer complete\n");
        send_response(client_fd, "226 Transfer complete.");
    }
}

void handle_mkd_command(int client_fd, char *dirname) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    // Create full path for the directory
    char dir_path[BUFFER_SIZE];
    sprintf(dir_path, "%s/%s", client_sessions[session_index].current_dir, dirname);
    
    // Check if directory already exists
    DIR *dir = opendir(dir_path);
    if (dir != NULL) {
        closedir(dir);
        send_response(client_fd, "550 Directory already exists.");
        return;
    }
    
    // Create directory
    if (mkdir(dir_path, 0777) == 0) {
        char response[BUFFER_SIZE];
        sprintf(response, "257 \"%s\" directory created.", dirname);
        send_response(client_fd, response);
    } else {
        send_response(client_fd, "550 Failed to create directory.");
    }
}

void handle_rmd_command(int client_fd, char *dirname) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    // Create full path for the directory
    char dir_path[BUFFER_SIZE];
    sprintf(dir_path, "%s/%s", client_sessions[session_index].current_dir, dirname);
    
    // Check if directory exists
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        send_response(client_fd, "550 Directory not found.");
        return;
    }
    closedir(dir);
    
    // Remove directory
    if (rmdir(dir_path) == 0) {
        char response[BUFFER_SIZE];
        sprintf(response, "250 \"%s\" directory removed.", dirname);
        send_response(client_fd, response);
    } else {
        send_response(client_fd, "550 Failed to remove directory. Make sure it is empty.");
    }
}

void handle_dele_command(int client_fd, char *filename) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    // Create full path for the file
    char file_path[BUFFER_SIZE];
    sprintf(file_path, "%s/%s", client_sessions[session_index].current_dir, filename);
    
    // Check if file exists
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
        send_response(client_fd, "550 File not found.");
        return;
    }
    fclose(fp);
    
    // Delete file
    if (unlink(file_path) == 0) {
        char response[BUFFER_SIZE];
        sprintf(response, "250 \"%s\" file deleted.", filename);
        send_response(client_fd, response);
    } else {
        send_response(client_fd, "550 Failed to delete file.");
    }
}

// Global variable to store old filename for RNFR/RNTO commands
char rnfr_filename[MAX_CLIENTS][BUFFER_SIZE];

void handle_rnfr_command(int client_fd, char *oldname) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    // Create full path for the file
    char file_path[BUFFER_SIZE];
    sprintf(file_path, "%s/%s", client_sessions[session_index].current_dir, oldname);
    
    // Check if file exists
    struct stat file_stat;
    if (stat(file_path, &file_stat) == -1) {
        send_response(client_fd, "550 File not found.");
        return;
    }
    
    // Store old filename for RNTO command
    sprintf(rnfr_filename[session_index], "%s", file_path);
    
    send_response(client_fd, "350 Requested file action pending further information.");
}

void handle_rnto_command(int client_fd, char *newname) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }
    
    // Check if RNFR command was sent
    if (rnfr_filename[session_index][0] == '\0') {
        send_response(client_fd, "503 Bad sequence of commands.");
        return;
    }
    
    // Create full path for the new file
    char new_file_path[BUFFER_SIZE];
    sprintf(new_file_path, "%s/%s", client_sessions[session_index].current_dir, newname);
    
    // Rename file
    if (rename(rnfr_filename[session_index], new_file_path) == 0) {
        char response[BUFFER_SIZE];
        sprintf(response, "250 File successfully renamed.");
        send_response(client_fd, response);
    } else {
        send_response(client_fd, "550 Failed to rename file.");
    }
    
    // Clear stored filename
    memset(rnfr_filename[session_index], 0, BUFFER_SIZE);
}