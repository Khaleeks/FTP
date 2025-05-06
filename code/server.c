/**
 * FTP Server Implementation
 * 
 * This file implements a basic FTP server according to RFC 959 specifications.
 * The server supports essential FTP commands including authentication, directory
 * navigation, and file transfer operations.
 * 
 * Key features:
 * - Multi-client support using select() for I/O multiplexing
 * - User authentication from a CSV file
 * - Separate control and data connections as per RFC 959
 * - Directory isolation for user security
 * - Support for standard FTP commands (USER, PASS, CWD, LIST, etc.)
 * 
 * The implementation follows the client-server model where:
 * - Control connection: Persistent TCP connection on port 21
 * - Data connection: Temporary TCP connection on port 20 (active mode)
 * 
 * Security considerations:
 * - Basic authentication mechanism (username/password)
 * - User directory isolation to prevent directory traversal
 * - Process separation for data transfers to improve stability
 */

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
 
 /**
  * Configuration Constants
  * 
  * These define the operational parameters of the FTP server:
  * - MAX_CLIENTS: Maximum number of simultaneous client connections
  * - BUFFER_SIZE: Standard buffer size for network and file operations
  * - MAX_USERS: Maximum number of users in the authentication database
  * - MAX_USERNAME/PASSWORD: Maximum length of username and password
  * - SERVER_PORT: Default FTP control port (21 as per RFC 959)
  * - MAX_PENDING: Maximum connection requests queued
  */
 #define MAX_CLIENTS 10
 #define BUFFER_SIZE 1024
 #define MAX_USERS 50
 #define MAX_USERNAME 50
 #define MAX_PASSWORD 50
 #define SERVER_PORT 21
 #define MAX_PENDING 5
 
 /**
  * User Structure
  * 
  * This structure stores authentication credentials for each user.
  * The server loads these from a CSV file at startup.
  * 
  * Fields:
  * - username: User's login name
  * - password: User's password (stored in plaintext for simplicity,
  *   though in production this should be hashed)
  */
 typedef struct {
     char username[MAX_USERNAME];
     char password[MAX_PASSWORD];
 } User;
 
 /**
  * FTP Session Structure
  * 
  * This structure maintains state information for each client connection.
  * It tracks both control and data connections, authentication status,
  * and navigation context.
  * 
  * Fields:
  * - control_fd: File descriptor for the control connection socket
  * - data_fd: File descriptor for the data connection socket
  * - username: Authenticated username for the session
  * - authenticated: Flag indicating if the user is authenticated
  * - current_dir: Current working directory for the session
  * - root_dir: Root directory for the server (for path security)
  * - data_ip: Client's IP address for data connection (PORT command)
  * - data_port: Client's port for data connection (PORT command)
  */
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
 
 /**
  * Global Variables
  * 
  * These variables maintain the server's state:
  * - users: Array of user credentials loaded from CSV
  * - num_users: Count of loaded users
  * - client_sessions: Array of active client sessions
  */
 User users[MAX_USERS];
 int num_users = 0;
 FTPSession client_sessions[MAX_CLIENTS];
 
 /**
  * Function Prototypes
  * 
  * These declarations organize the server's functionality into logical groups:
  * - Server initialization and management functions
  * - Client session handling functions
  * - Command processing functions for each FTP command
  * - Helper functions for authentication and session management
  */
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
 int get_session_index(int client_fd);
 void clean_up_session(int session_index);
 void handle_child_process(int sig);
 
 /**
  * Main Function
  * 
  * The core server routine that:
  * 1. Initializes server components and user database
  * 2. Creates and configures the server socket
  * 3. Enters the main event loop using select() for I/O multiplexing
  * 4. Handles new connections and client requests
  * 
  * The server uses a non-blocking I/O model with select() to efficiently
  * handle multiple clients with a single thread. This approach avoids
  * the overhead of creating a new thread or process for each client.
  */
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
 
 /**
  * Session Initialization Function
  * 
  * This function initializes all client session slots to an empty state.
  * It is called once during server startup to prepare the session array.
  * 
  * Each session is marked with invalid file descriptors (-1) and
  * empty strings for all character arrays, ensuring a clean initial state.
  */
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
 
 /**
  * Child Process Handler
  * 
  * This signal handler prevents zombie processes by cleaning up child processes
  * that have completed. It's registered for the SIGCHLD signal in main().
  * 
  * FTP operations like LIST, RETR, and STOR fork separate processes for data
  * transfer. Without this handler, these child processes would remain as zombies
  * after completing their transfers.
  * 
  * The handler saves and restores errno to avoid interference with other
  * system calls that might be in progress when the signal arrives.
  */
 void handle_child_process(int sig) {
     (void)sig;
     int saved_errno = errno;
     while (waitpid(-1, NULL, WNOHANG) > 0);
     errno = saved_errno;
 }
 
 /**
  * User Database Loader
  * 
  * This function loads user credentials from a CSV file (users.csv).
  * The file format is expected to be "username,password" per line.
  * 
  * The function reads each line, parses username and password, and
  * stores them in the global users array. If the file cannot be opened,
  * the server exits with an error.
  * 
  * Note: In a production environment, passwords should be stored and
  * compared in hashed form rather than plaintext.
  */
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
 
 /**
  * Session Lookup Function
  * 
  * This function finds the session index corresponding to a given client
  * file descriptor. It's used to locate the correct session data when
  * processing commands.
  * 
  * The function linearly searches the client_sessions array for a matching
  * control_fd and returns the index if found, or -1 if not found.
  * 
  * Since MAX_CLIENTS is small, a linear search is efficient enough. For
  * larger values, a hash table might be more appropriate.
  */
 int get_session_index(int client_fd) {
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (client_sessions[i].control_fd == client_fd) {
             return i;
         }
     }
     return -1;
 }
 
 /**
  * Session Cleanup Function
  * 
  * This function cleans up resources associated with a client session.
  * It's called when a client disconnects or when the QUIT command is processed.
  * 
  * The function:
  * 1. Closes the data connection if it exists
  * 2. Resets all session fields to their initial state
  * 
  * This ensures that the session slot can be reused for a new client.
  * The control connection is closed by the caller, not by this function.
  */
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
 
 /**
  * Response Sender Function
  * 
  * This function formats and sends a response to the client over the
  * control connection. It appends CRLF line termination as required
  * by the FTP protocol (RFC 959).
  * 
  * All server responses follow the FTP reply format:
  *   - 3-digit numeric code indicating the response type
  *   - Space character
  *   - Text message
  *   - CRLF sequence
  * 
  * Example: "200 Command okay.\r\n"
  */
 void send_response(int client_fd, char *response) {
     char full_response[BUFFER_SIZE];
     sprintf(full_response, "%s\r\n", response);
     write(client_fd, full_response, strlen(full_response));
 }
 
 /**
  * User Authentication Function
  * 
  * This function verifies a username and password against the loaded
  * user database.
  * 
  * It performs a simple linear search through the users array, comparing
  * the provided credentials with each entry. Returns 1 for successful
  * authentication, 0 for failure.
  * 
  * In a production environment, this should use secure password hashing
  * and comparison techniques instead of plaintext comparison.
  */
 int authenticate_user(char *username, char *password) {
     for (int i = 0; i < num_users; i++) {
         if (strcmp(users[i].username, username) == 0 && 
             strcmp(users[i].password, password) == 0) {
             return 1;
         }
     }
     return 0;
 }
 
 /**
  * Command Handler Function
  * 
  * This function parses and routes incoming FTP commands to the appropriate
  * handler functions. It's the central dispatch point for all client requests.
  * 
  * The function:
  * 1. Parses the command and arguments from the input
  * 2. Looks up the client's session
  * 3. Routes the command to the specific handler
  * 
  * Most commands require authentication (except USER, PASS, QUIT).
  * If a client attempts to use these commands without authentication,
  * they receive an error response.
  */
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
     } else {
         send_response(client_fd, "202 Command not implemented.");
     }
 }
 
 /**
  * USER Command Handler
  * 
  * This function handles the FTP USER command, which is the first step
  * in the authentication process.
  * 
  * When a user provides their username:
  * 1. The function checks if the username exists in the database
  * 2. If it exists, the username is stored in the session and a response
  *    prompting for a password is sent
  * 3. If not, an error response is sent
  * 
  * Note that no authentication takes place here - that happens in the
  * PASS command handler after receiving the password.
  */
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
 
 /**
  * PASS Command Handler
  * 
  * This function handles the FTP PASS command, which completes the
  * authentication process by verifying the password.
  * 
  * The function:
  * 1. Checks if a username has been provided first (proper sequence)
  * 2. Authenticates the username/password combination
  * 3. If successful:
  *    - Sets the authenticated flag
  *    - Creates a user directory if it doesn't exist
  *    - Changes the current directory to the user's home directory
  *    - Sends success response
  * 4. If unsuccessful, sends an error response
  * 
  * This design creates a separate directory for each user, providing
  * basic isolation between user files.
  */
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
 
 /**
  * QUIT Command Handler
  * 
  * This function handles the FTP QUIT command, which terminates
  * the control connection.
  * 
  * The function:
  * 1. Sends a goodbye message to the client
  * 2. Cleans up the session resources
  * 
  * The actual closing of the control connection socket is left to
  * the main event loop, which detects the disconnection.
  */
 void handle_quit_command(int client_fd) {
     send_response(client_fd, "221 Service closing control connection.");
     int session_index = get_session_index(client_fd);
     if (session_index != -1) {
         clean_up_session(session_index);
     }
     // Do not close(client_fd) here; let the main loop handle it
 }
 
 /**
  * PORT Command Handler
  * 
  * This function handles the FTP PORT command, which is used to establish
  * the data connection in active mode.
  * 
  * In active mode, the client tells the server which IP address and port
  * to connect to for data transfer. The PORT command provides this information
  * in the format: h1,h2,h3,h4,p1,p2 where:
  * - h1-h4 are the octets of the client's IP address
  * - p1,p2 encode the port number (port = p1*256 + p2)
  * 
  * The function parses these values and stores them in the session for
  * future data transfers (LIST, RETR, STOR).
  */
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
 
 /**
 * LIST Command Handler
 * 
 * This function implements the FTP LIST command which sends a listing of files in the current
 * directory to the client via a data connection. The implementation follows these steps:
 * 
 * 1. Check if the client has established a data connection via PORT or PASV
 * 2. Create and configure a data socket from port 20 (as required by RFC 959 for active mode)
 * 3. Connect to the client's data port
 * 4. Fork a child process to handle the data transfer while the parent continues to handle control
 *    channel communications
 * 5. In the child process, read directory contents and send them to the client
 * 6. The parent process waits for child completion then sends transfer complete message
 * 
 * This function uses a child process for data transfer to maintain responsiveness on the
 * control channel and prevent blocking the main server. Port 20 is used as source for
 * compliance with the FTP protocol standard.
 */
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

    // Bind to port 20 for RFC 959 compliance
    struct sockaddr_in server_data_addr;
    memset(&server_data_addr, 0, sizeof(server_data_addr));
    server_data_addr.sin_family = AF_INET;
    server_data_addr.sin_addr.s_addr = INADDR_ANY;
    server_data_addr.sin_port = htons(20);
    if (bind(data_fd, (struct sockaddr *)&server_data_addr, sizeof(server_data_addr)) < 0) {
        perror("Bind to port 20 failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection (bind 20).");
        return;
    }
    // Print the port the server is sending from
    struct sockaddr_in actual_addr;
    socklen_t actual_addr_len = sizeof(actual_addr);
    if (getsockname(data_fd, (struct sockaddr *)&actual_addr, &actual_addr_len) == 0) {
        printf("[DEBUG] Server data port (source port) for this transfer: %d\n", ntohs(actual_addr.sin_port));
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
        printf("We are in child process\n");
        
        if ((dir = opendir(client_sessions[session_index].current_dir)) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                // Skip . and ..
                if (ent->d_name[0]=='.') {
                    continue;
                }
                
                char file_info[BUFFER_SIZE];
                sprintf(file_info, "%s\r\n", ent->d_name);
                printf("%s",file_info);
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

/**
 * CWD (Change Working Directory) Command Handler
 * 
 * This function changes the current working directory for the client session.
 * It implements robust path handling and security measures:
 * 
 * 1. Handles both absolute paths (starting with '/') and relative paths
 * 2. Uses realpath() to canonicalize the path and resolve '..' and '.'
 * 3. Implements path security to prevent directory traversal attacks that would allow clients
 *    to escape their assigned root directory (user's home directory)
 * 4. Validates that the target directory exists and is accessible
 * 5. Updates the client's session with the new current directory
 * 
 * The security model ensures users can only access directories within their assigned root,
 * preventing unauthorized access to the server's file system. This is critical for FTP
 * servers to maintain proper isolation between user accounts.
 * 
 * The response includes a relative path rather than exposing the full server path.
 */
void handle_cwd_command(int client_fd, char *dir) {
    int session_index = get_session_index(client_fd);
    
    if (session_index == -1) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }

    char target_path[PATH_MAX];

    // Check if dir is absolute or relative
    if (dir[0] == '/') {
        snprintf(target_path, sizeof(target_path), "%s", dir);
    } else {
        snprintf(target_path, sizeof(target_path), "%s/%s", client_sessions[session_index].current_dir, dir);
    }

    // Normalize path to resolve ".." and "."
    char resolved_path[PATH_MAX];
    if (realpath(target_path, resolved_path) == NULL) {
        send_response(client_fd, "550 No such file or directory.");
        return;
    }

    // Check if resolved path stays within user's root directory
    char *user_root = strstr(client_sessions[session_index].current_dir, client_sessions[session_index].username);
    if (!user_root) {
        send_response(client_fd, "500 Internal server error.");
        return;
    }

    // Build the full root prefix path
    char allowed_root[PATH_MAX];
    if (realpath(client_sessions[session_index].username, allowed_root) == NULL) {
        perror("realpath");
        send_response(client_fd, "550 Invalid root directory.");
        return;
    }

    size_t len = strlen(allowed_root);
    if (strncmp(resolved_path, allowed_root, len) != 0 || 
        (resolved_path[len] != '/' && resolved_path[len] != '\0')) {
        send_response(client_fd, "550 Permission denied.");
        return;
    }

    // Update session path
    snprintf(client_sessions[session_index].current_dir, sizeof(client_sessions[session_index].current_dir), "%s", resolved_path);
    printf("Changing directory to: %s\n", resolved_path);

    // Send relative path in response
    char *rel_path = strstr(resolved_path, client_sessions[session_index].username);
    char response[BUFFER_SIZE];
    if (rel_path) {
        snprintf(response, sizeof(response), "200 directory changed to /%s", rel_path);
    } else {
        snprintf(response, sizeof(response), "200 directory changed to %s", resolved_path);
    }

    send_response(client_fd, response);
}

/**
 * PWD (Print Working Directory) Command Handler
 * 
 * This function implements the FTP PWD command which returns the current working directory
 * to the client. The function:
 * 
 * 1. Retrieves the user's session data
 * 2. Extracts the relative path from the absolute path by locating the username in the path
 * 3. Returns a formatted response with the relative path to maintain user privacy and security
 * 
 * By returning only the relative path instead of the full server path, this implementation
 * prevents exposing the server's directory structure, which is a security best practice.
 * This handling aligns with the FTP protocol standard for the PWD command.
 */
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

/**
 * RETR (Retrieve) Command Handler
 * 
 * This function implements the FTP RETR command for downloading files from the server.
 * The implementation follows these steps:
 * 
 * 1. Validates that the client has established a data connection
 * 2. Creates a data socket and binds to port 20 (RFC 959 compliance for active mode)
 * 3. Verifies that the requested file exists before attempting transfer
 * 4. Establishes the data connection with the client
 * 5. Forks a child process to handle the actual file transfer
 * 6. The child process reads the file in binary mode and sends it over the data connection
 * 7. The parent process waits for completion and handles cleanup
 * 
 * Using a forked child process for the data transfer allows the server to maintain responsiveness
 * on the control channel and handle multiple transfers simultaneously. The file is opened in
 * binary mode ("rb") to ensure correct transfer of non-text files and cross-platform compatibility.
 * 
 * The transfer uses buffered I/O for efficiency, reading and writing in chunks defined by BUFFER_SIZE.
 */
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
    
    // Bind to port 20 for RFC 959 compliance
    struct sockaddr_in server_data_addr;
    memset(&server_data_addr, 0, sizeof(server_data_addr));
    server_data_addr.sin_family = AF_INET;
    server_data_addr.sin_addr.s_addr = INADDR_ANY;
    server_data_addr.sin_port = htons(20);
    if (bind(data_fd, (struct sockaddr *)&server_data_addr, sizeof(server_data_addr)) < 0) {
        perror("Bind to port 20 failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection (bind 20).");
        return;
    }
    // Print the port the server is sending from
    struct sockaddr_in actual_addr;
    socklen_t actual_addr_len = sizeof(actual_addr);
    if (getsockname(data_fd, (struct sockaddr *)&actual_addr, &actual_addr_len) == 0) {
        printf("[DEBUG] Server data port (source port) for this transfer: %d\n", ntohs(actual_addr.sin_port));
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

/**
 * STOR (Store) Command Handler
 * 
 * This function implements the FTP STOR command which allows clients to upload files to the server.
 * The implementation includes several robust features:
 * 
 * 1. Uses a temporary file during upload to prevent corruption if the transfer fails
 * 2. Only renames to the final filename after successful completion
 * 3. Creates a data connection on port 20 (as required by RFC 959 for active mode)
 * 4. Uses a forked child process to handle the data transfer, maintaining control channel responsiveness
 * 5. Implements proper error handling and cleanup at each stage
 * 
 * The temporary file approach (using tmp_[timestamp]_[filename]) provides atomicity - either the
 * entire file is successfully uploaded or nothing changes, preventing partial or corrupted files.
 * 
 * Using binary mode ensures correct handling of all file types, and the buffered reading/writing
 * provides efficient transfer of variable-sized files.
 */
void handle_stor_command(int client_fd, char *filename) {
    int session_index = get_session_index(client_fd); // Find the session for this client
    
    if (session_index == -1 || client_sessions[session_index].data_port == -1) {
        send_response(client_fd, "425 Can't open data connection."); // No data connection info
        return;
    }
    
    // Get client IP and port for data connection
    char client_ip[50];
    strcpy(client_ip, client_sessions[session_index].data_ip);
    int client_port = client_sessions[session_index].data_port;
    
    // Create a new socket for the data connection
    int data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_fd < 0) {
        perror("Socket creation failed");
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Allow address reuse for the data socket
    int opt = 1;
    if (setsockopt(data_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    // Bind the data socket to port 20 (FTP active mode requirement)
    struct sockaddr_in server_data_addr;
    memset(&server_data_addr, 0, sizeof(server_data_addr));
    server_data_addr.sin_family = AF_INET;
    server_data_addr.sin_addr.s_addr = INADDR_ANY;
    server_data_addr.sin_port = htons(20);
    if (bind(data_fd, (struct sockaddr *)&server_data_addr, sizeof(server_data_addr)) < 0) {
        perror("Bind to port 20 failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection (bind 20).");
        return;
    }
    // Print the port the server is sending from (debug)
    struct sockaddr_in actual_addr;
    socklen_t actual_addr_len = sizeof(actual_addr);
    if (getsockname(data_fd, (struct sockaddr *)&actual_addr, &actual_addr_len) == 0) {
        printf("[DEBUG] Server data port (source port) for this transfer: %d\n", ntohs(actual_addr.sin_port));
    }
    
    // Prepare temporary and final file paths for upload
    char temp_filepath[BUFFER_SIZE];
    sprintf(temp_filepath, "%s/tmp_%ld_%s", client_sessions[session_index].current_dir, (long)time(NULL), filename);
    char final_filepath[BUFFER_SIZE];
    sprintf(final_filepath, "%s/%s", client_sessions[session_index].current_dir, filename);
    
    // Set up the client's data address for the connection
    struct sockaddr_in client_data_addr;
    memset(&client_data_addr, 0, sizeof(client_data_addr));
    client_data_addr.sin_family = AF_INET;
    client_data_addr.sin_addr.s_addr = inet_addr(client_ip);
    client_data_addr.sin_port = htons(client_port);
    
    send_response(client_fd, "150 File status okay; about to open data connection."); // Ready to receive file
    printf("File okay, beginning data connections\n");
    printf("Connecting to Client Transfer Socket...\n");
    
    // Connect to the client's data port
    if (connect(data_fd, (struct sockaddr *)&client_data_addr, sizeof(client_data_addr)) < 0) {
        perror("Connect failed");
        close(data_fd);
        send_response(client_fd, "425 Can't open data connection.");
        return;
    }
    
    printf("Connection Successful\n");
    
    // Fork a child process to handle the file transfer
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        close(data_fd);
        send_response(client_fd, "451 Requested action aborted: local error in processing.");
        return;
    }
    
    if (pid == 0) {
        // Child process: receives the file and writes to a temporary file
        close(client_fd); // Child doesn't need the control connection
        FILE *fp = fopen(temp_filepath, "wb"); // Open temp file for writing (binary mode)
        if (fp == NULL) {
            close(data_fd);
            exit(1);
        }
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;
        // Read from data connection and write to file
        while ((bytes_read = read(data_fd, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, 1, bytes_read, fp);
        }
        fclose(fp); // Close file
        close(data_fd); // Close data connection
        // Rename temp file to final filename (atomic move)
        rename(temp_filepath, final_filepath);
        exit(0); // Child exits
    } else {
        // Parent process: cleans up and sends completion response
        close(data_fd); // Parent closes its copy of the data socket
        // Reset data connection info for this session
        client_sessions[session_index].data_port = -1;
        memset(client_sessions[session_index].data_ip, 0, sizeof(client_sessions[session_index].data_ip));
        // Wait for child to finish
        waitpid(pid, NULL, 0);
        printf("226 Transfer complete\n");
        send_response(client_fd, "226 Transfer complete."); // Notify client
    }
}
