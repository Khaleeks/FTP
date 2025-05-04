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

/*
 * CONSTANTS AND SETTINGS
 * ----------------------
 * These define important values used throughout the program:
 * - BUFFER_SIZE: Maximum size for data buffers (1024 bytes)
 * - SERVER_PORT: Default FTP control port (21)
 * - SERVER_IP: The server address (localhost)
 * - MAXPORT: Maximum allowed port number
 */
#define BUFFER_SIZE 1024
#define SERVER_PORT 21
#define SERVER_IP "127.0.0.1"
#define MAXPORT 65530

/*
 * GLOBAL VARIABLES
 * ---------------
 * - control_fd: File descriptor for the main control connection to the FTP server
 * - data_port: Port number for data transfers (file listings, uploads, downloads)
 * - authenticated: Flag to track if the user has successfully logged in
 */
int control_fd;
int data_port = 0;
int authenticated = 0;

/*
 * FUNCTION DECLARATIONS
 * --------------------
 * These declarations tell the compiler about all the functions that will be
 * defined later in the code. Each function handles a specific part of the
 * FTP client's operation.
 */
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
int count_arguments(char *command);

/*
 * MAIN FUNCTION
 * ------------
 * This is where the program begins execution. It creates a connection to
 * the FTP server and enters a loop to process user commands.
 */
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
    
    /*
     * MAIN COMMAND LOOP
     * ----------------
     * This continuous loop accepts user input, parses commands, and calls
     * the appropriate handler functions until the user enters the QUIT command.
     */
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
        char cmd[BUFFER_SIZE] = "";
        char arg[BUFFER_SIZE] = "";
        
        if (sscanf(command, "%s %s", cmd, arg) < 1) {
            continue;
        }

        // Check for extra parameters
        int arg_count = count_arguments(command);
        
        /*
         * COMMAND PROCESSING
         * -----------------
         * This section checks which command the user entered and calls 
         * the appropriate handler function after validating the arguments.
         * Each command has specific requirements for the number of arguments.
         */
        if (strcmp(cmd, "USER") == 0) {
            if (arg_count != 2) {
                printf("Usage: USER <username>\n");
            } else {
                handle_user_command(arg);
            }
        } else if (strcmp(cmd, "PASS") == 0) {
            if (arg_count != 2) {
                printf("Usage: PASS <password>\n");
            } else {
                handle_pass_command(arg);
            }
        } else if (strcmp(cmd, "LIST") == 0) {
            if (arg_count != 1) {
                printf("Usage: LIST\n");
            } else {
                handle_list_command();
            }
        } else if (strcmp(cmd, "!LIST") == 0) {
            if (arg_count != 1) {
                printf("Usage: !LIST\n");
            } else {
                handle_local_list_command();
            }
        } else if (strcmp(cmd, "CWD") == 0) {
            if (arg_count != 2) {
                printf("Usage: CWD <directory>\n");
            } else {
                handle_cwd_command(arg);
            }
        } else if (strcmp(cmd, "!CWD") == 0) {
            if (arg_count != 2) {
                printf("Usage: !CWD <directory>\n");
            } else {
                handle_local_cwd_command(arg);
            }
        } else if (strcmp(cmd, "PWD") == 0) {
            if (arg_count != 1) {
                printf("Usage: PWD\n");
            } else {
                handle_pwd_command();
            }
        } else if (strcmp(cmd, "!PWD") == 0) {
            if (arg_count != 1) {
                printf("Usage: !PWD\n");
            } else {
                handle_local_pwd_command();
            }
        } else if (strcmp(cmd, "RETR") == 0) {
            if (arg_count != 2) {
                printf("Usage: RETR <filename>\n");
            } else {
                handle_retr_command(arg);
            }
        } else if (strcmp(cmd, "STOR") == 0) {
            if (arg_count != 2) {
                printf("Usage: STOR <filename>\n");
            } else {
                handle_stor_command(arg);
            }
        } else if (strcmp(cmd, "QUIT") == 0) {
            if (arg_count != 1) {
                printf("Usage: QUIT\n");
            } else {
                handle_quit_command();
                break;
            }
        } else {
            printf("Unknown command: %s\n", cmd);
            printf("Available commands: USER, PASS, LIST, !LIST, CWD, !CWD, PWD, !PWD, RETR, STOR, QUIT\n");
        }
    }
    
    return 0;
}

/*
 * ARGUMENT COUNTING FUNCTION
 * ------------------------
 * This function counts how many space-separated words are in a command,
 * which helps validate that commands have the correct number of arguments.
 */
int count_arguments(char *command) {
    int count = 0;
    char *token;
    char temp[BUFFER_SIZE];
    
    // Create a copy of the command to avoid modifying the original
    strncpy(temp, command, BUFFER_SIZE);
    
    // Count tokens (space-separated words)
    token = strtok(temp, " \t");
    while (token != NULL) {
        count++;
        token = strtok(NULL, " \t");
    }
    
    return count;
}

/*
 * WELCOME MESSAGE FUNCTION
 * ---------------------
 * This function displays instructions for the user when the program starts,
 * explaining how to log in and what commands are available.
 */
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

/*
 * SERVER COMMUNICATION FUNCTIONS
 * ---------------------------
 * These two functions handle the basic communication with the FTP server:
 * - send_command: Sends a formatted command to the server
 * - receive_response: Reads and processes the server's response
 */
void send_command(char *command) {
    char full_command[BUFFER_SIZE];
    sprintf(full_command, "%s\r\n", command);
    write(control_fd, full_command, strlen(full_command));
}

void receive_response(char *response, int size) {
    memset(response, 0, size);
    int bytes_read = read(control_fd, response, size - 1);
    if(bytes_read > 0) {
        response[bytes_read] = '\0';
        // Remove trailing newline
        response[strcspn(response, "\r\n")] = '\0';
    }
}

/*
 * DATA CONNECTION SETUP
 * -------------------
 * This function creates a data connection for transferring files or directory listings.
 * In FTP, there are two connections:
 * 1. Control connection (established in main) for sending commands
 * 2. Data connection (created here) for transferring actual files/data
 *
 * This function:
 * - Creates a new socket
 * - Binds it to a random available port
 * - Tells the FTP server which port to connect to using the PORT command
 */
int setup_data_connection() {
    struct sockaddr_in data_addr;
    socklen_t addr_len = sizeof(data_addr);
    int data_listen_fd;
    
    // Create data socket
    if ((data_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Data socket creation failed");
        return -1;
    }
    // Initialize data address
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0;  // Let the system assign a port
    
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(data_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_listen_fd);
        return -1;
    }
    
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
    
    // Return data socket
    return data_listen_fd;
}

/*
 * AUTHENTICATION HANDLERS
 * ---------------------
 * These functions manage the login process:
 * - handle_user_command: Sends the username to the server
 * - handle_pass_command: Sends the password and updates authentication status
 */
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

/*
 * LIST COMMAND HANDLERS
 * -------------------
 * These functions show directory contents:
 * - handle_list_command: Shows files on the server
 * - handle_local_list_command: Shows files on the local computer
 */
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
    
    // get response.
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);

    if (strncmp(response, "530", 3) == 0 || response[0] == '5') {
        //the command wasn't successful present the ftp> prompt to the user again
        close(data_fd);
        return;  
    }

    //Accept data connections
    struct sockaddr_in server_data_addr;
    socklen_t server_addr_len = sizeof(server_data_addr);
    
    int connection_fd = accept(data_fd, (struct sockaddr *)&server_data_addr, &server_addr_len);
    if (connection_fd < 0) {
        perror("Data accept failed");
        close(data_fd);
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    while ((bytes_read = read(connection_fd, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    if (bytes_read < 0) {
        fprintf(stderr, "Error receiving directory listing: %s\n", strerror(errno));
    }
    
    // Close data connection
    close(connection_fd);
    close(data_fd);
    
    // Read the final status response ("226" message)
    char final_response[BUFFER_SIZE];
    receive_response(final_response, BUFFER_SIZE);
    printf("%s\n", final_response);
}

void handle_local_list_command() {
    // Execute shell command
    system("ls");
}

/*
 * DIRECTORY NAVIGATION HANDLERS
 * --------------------------
 * These functions help change or show the current directory:
 * - handle_cwd_command: Changes directory on the server
 * - handle_local_cwd_command: Changes directory on the local computer
 * - handle_pwd_command: Shows current directory on the server
 * - handle_local_pwd_command: Shows current directory on the local computer
 */
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
    } else {
        printf("Local directory changed to %s\n", dir);
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

/*
 * FILE TRANSFER HANDLERS
 * --------------------
 * These functions manage downloading and uploading files:
 * - handle_retr_command: Downloads a file from the server
 * - handle_stor_command: Uploads a file to the server
 *
 * Both functions work by:
 * 1. Setting up a data connection
 * 2. Sending the appropriate command (RETR or STOR)
 * 3. Opening the local file (for reading or writing)
 * 4. Transferring data between the file and the data connection
 * 5. Closing all connections and files
 */
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
        
        //Accept data connections
        struct sockaddr_in server_data_addr;
        socklen_t server_addr_len = sizeof(server_data_addr);
        
        int connection_fd = accept(data_fd, (struct sockaddr *)&server_data_addr, &server_addr_len);
        if (connection_fd < 0) {
            perror("Data accept failed");
            close(data_fd);
            fclose(fp);
            return;
        }

        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        // Receive file
        while ((bytes_read = read(connection_fd, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, 1, bytes_read, fp);
        }
        if (bytes_read < 0) {
            fprintf(stderr, "Error receiving file: %s\n", strerror(errno));
        }
        
        fclose(fp);
        
        // Close data connection
        close(connection_fd);
        close(data_fd);
        
        // Receive final response
        char final_response[BUFFER_SIZE];
        receive_response(final_response, BUFFER_SIZE);
        printf("%s\n", final_response);
    } else {
        close(data_fd);
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
        
        //Accept data connections
        struct sockaddr_in server_data_addr;
        socklen_t server_addr_len = sizeof(server_data_addr);
        
        int connection_fd = accept(data_fd, (struct sockaddr *)&server_data_addr, &server_addr_len);
        if (connection_fd < 0) {
            perror("Data accept failed");
            close(data_fd);
            fclose(fp);
            return;
        }

        // Send file
        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            write(connection_fd, buffer, bytes_read);
        }
        
        fclose(fp);
        
        // Close data connection
        close(connection_fd);
        close(data_fd);
        
        // Receive final response
        char final_response[BUFFER_SIZE];
        receive_response(final_response, BUFFER_SIZE);
        printf("%s\n", final_response);
    } else {
        close(data_fd);
    }
}

/*
 * QUIT COMMAND HANDLER
 * -----------------
 * This function cleanly terminates the connection to the FTP server
 * by sending the QUIT command and closing the control connection.
 */
void handle_quit_command() {
    send_command("QUIT");
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("%s\n", response);
    
    close(control_fd);
}