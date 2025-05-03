#include <stdio.h>      /* Standard I/O functions (printf, fopen, etc.) */
#include <stdlib.h>     /* Standard library functions (malloc, free, etc.) */
#include <string.h>     /* String manipulation functions */
#include <errno.h>      /* For errno and error codes */
#include <signal.h>     /* For signal handling (SIGINT) */
#include <stdbool.h>    /* For bool type (C99+) */
#include <sys/stat.h>   /* For stat() and file information */
#include <ctype.h>      /* For character type functions (isalpha, isdigit, etc.) */
#include <stdint.h>     /* For fixed-width integer types (uint16_t) */

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>   /* Windows socket API (must come before windows.h) */
#include <ws2tcpip.h>   /* Windows socket extensions */
#include <direct.h>     /* For _mkdir, _access */
#include <io.h>         /* For _access on some compilers */

typedef SOCKET socket_t;      /* Abstract socket type (UINT_PTR on Windows) */
typedef int socklen_t;        /* Socket address length type */
// Note: ssize_t should be defined by MinGW headers now, removed manual typedef
#define CLOSE_SOCKET closesocket   /* Abstraction for closing sockets */
// Rely on <sys/stat.h> for stat, S_ISREG, S_ISDIR
#define PATH_SEPARATOR '\\'        /* Directory separator character */
#define MKDIR(path) _mkdir(path)   /* Directory creation function */
#define F_OK 0                     /* File existence test flag for access() */
#define access _access             /* Map to Windows file access check */
#define strcasecmp _stricmp        /* Map to Windows case-insensitive string compare */
// Link with -lws2_32 using compiler flag
#else // POSIX
#include <unistd.h>     /* POSIX API (close, unlink, access, etc.) */
#include <sys/socket.h> /* POSIX socket API */
#include <netinet/in.h> /* Internet address structures */
#include <arpa/inet.h>  /* inet_addr and related functions */
#include <fcntl.h>      /* File control options */
#include <strings.h>    /* For strcasecmp on POSIX */

typedef int socket_t;         /* Abstract socket type (file descriptor on POSIX) */
#define INVALID_SOCKET (-1)   /* Error value for socket creation */
#define SOCKET_ERROR (-1)     /* Error return value for socket operations */
#define CLOSE_SOCKET close    /* Abstraction for closing sockets */
#define PATH_SEPARATOR '/'    /* Directory separator character */
#define MKDIR(path) mkdir(path, 0755)  /* Directory creation with permissions */
#endif

// --- Constants ---
#define BUFFER_SIZE 4096      /* Size of buffer for reading HTTP requests */
#define MAX_CONNECTIONS 20    /* Maximum backlog of pending connections */
#define MAX_PATH_LEN 512      /* Maximum length of file paths */
#define DEFAULT_PORT 8080     /* Default HTTP port if not specified */
#define DEFAULT_IP "0.0.0.0"  /* Default listening address (all interfaces) */
#define SERVER_VERSION "CMarkdownServ/0.7" /* Server identifier and version */

// --- Global Configuration ---
/**
 * @brief Rendering mode enumeration
 * 
 * Defines the two possible rendering modes for the server:
 * - RENDER_FRONTEND: Client-side rendering with marked.js (default)
 * - RENDER_BACKEND: Server-side rendering with internal markdown parser
 */
typedef enum
{
    RENDER_FRONTEND,         /* Client-side rendering using marked.js */
    RENDER_BACKEND           /* Server-side rendering using internal renderer */
} RenderMode;

/**
 * @brief Global rendering mode (frontend or backend)
 * 
 * Controls whether Markdown is rendered on the client side (RENDER_FRONTEND)
 * using marked.js or on the server side (RENDER_BACKEND) using the internal
 * renderer. Set via the --render command-line option.
 */
RenderMode G_render_mode = RENDER_FRONTEND;

/**
 * @brief Base directory for serving files
 * 
 * Root directory from which .md files are served. All relative paths
 * requested by clients are resolved relative to this directory.
 * Set via the --dir command-line option.
 */
const char *G_base_dir = ".";

/**
 * @brief IP address to listen on
 * 
 * The server binds to this IP address to listen for incoming connections.
 * Default is "0.0.0.0" (all interfaces). Set via the --ip command-line option.
 */
const char *G_listen_ip_str = DEFAULT_IP;

/**
 * @brief Port number to listen on
 * 
 * The TCP port on which the server listens for incoming HTTP connections.
 * Default is 8080. Set via the --port command-line option.
 */
uint16_t G_listen_port = DEFAULT_PORT;

/**
 * @brief Verbosity level for logging
 * 
 * Controls how much information is logged:
 * 0 = quiet (errors only)
 * 1 = info (basic operations)
 * 2 = detail (more operational detail) 
 * 3 = debug (verbose diagnostic information)
 * Set via -v, -vv, -vvv command-line options.
 */
int G_verbose_level = 0; // 0=quiet, 1=info, 2=detail, 3=debug

// --- Platform-specific Socket Format Specifier ---
#ifdef _WIN32
#define SOCKET_FMT "%llu"    /* Format specifier for SOCKET on Windows (unsigned long long) */
#define SOCKET_CAST(s) ((unsigned long long)(s)) /* Cast SOCKET to unsigned long long for printing */
#else
#define SOCKET_FMT "%d"      /* Format specifier for socket fd on POSIX (int) */
#define SOCKET_CAST(s) (s)   /* No cast needed for POSIX sockets */
#endif

// --- Log Macros ---
#define LOG_INFO(...)                      \
    do                                     \
    {                                      \
        if (G_verbose_level >= 1)          \
        {                                  \
            printf("[INFO] " __VA_ARGS__); \
            fflush(stdout);                \
        }                                  \
    } while (0)                           /* Basic informational messages */

#define LOG_DETAIL(...)                      \
    do                                       \
    {                                        \
        if (G_verbose_level >= 2)            \
        {                                    \
            printf("[DETAIL] " __VA_ARGS__); \
            fflush(stdout);                  \
        }                                    \
    } while (0)                             /* Detailed operational messages */

#define LOG_DEBUG(...)                      \
    do                                      \
    {                                       \
        if (G_verbose_level >= 3)           \
        {                                   \
            printf("[DEBUG] " __VA_ARGS__); \
            fflush(stdout);                 \
        }                                   \
    } while (0)                            /* Low-level debug information */

#define LOG_ERROR(...)                           \
    do                                           \
    {                                            \
        fprintf(stderr, "[ERROR] " __VA_ARGS__); \
        fflush(stderr);                          \
    } while (0)                                 /* Error messages (always print) */

// --- Embedded index.html (for Frontend Mode - Added Header) ---
/**
 * @brief Embedded HTML content for frontend rendering mode
 * 
 * Contains the complete HTML shell that is sent to clients in frontend mode.
 * Includes CSS styles, JavaScript for fetching and rendering Markdown using 
 * marked.js, and the top navigation header with home link.
 */
const char *html_index_content =
    "<!doctype html><html><head><meta charset=\"utf-8\"/><title>C Dynamic Markdown Server</title>"
    "<style>"
    "body{font-family:sans-serif;line-height:1.6;padding:20px;max-width:800px;margin:auto;padding-top:50px;position:relative;}" // Added padding-top and relative position
    ".top-header{position:absolute;top:10px;left:20px;font-size:0.9em;}"                                                        // Style for header
    ".top-header a{text-decoration:none;color:#007bff;}"
    ".top-header a:hover{text-decoration:underline;}"
    "#content{border:1px solid #eee;padding:1em 2em;background-color:#f9f9f9;border-radius:5px;min-height:100px}"
    ".loading{font-style:italic;color:#888}"
    ".error-message{color:red;font-weight:bold}"
    ".status-code{font-family:monospace;background-color:#eee;padding:.1em .3em;border-radius:3px}"
    "</style></head>"
    "<body>"
    "<div class=\"top-header\"><a href=\"/\">← Home</a></div>" // Added Header Link
    "<div id=\"content\"><p class=loading>Loading content...</p></div>"
    "<script src=\"https://cdn.jsdelivr.net/npm/marked/marked.min.js\"></script>"
    "<script>const c=document.getElementById('content'),p=window.location.pathname;let m=(p==='/')?'/index.md':`${p.endsWith('/')?p.slice(0,-1):p}.md`;c.querySelector('.loading').textContent=`Loading content from ${m}...`;console.log(`Requesting Markdown: ${m}`);fetch(m).then(r=>{if(!r.ok)throw new Error(`HTTP error! Status: <span class=status-code>${r.status} ${r.statusText}</span> trying <span class=status-code>${m}</span>`);return r.text()}).then(t=>{marked.use({gfm:!0});c.innerHTML=marked.parse(t)}).catch(e=>{c.innerHTML=`<p class=error-message>Error loading/rendering:</p><p>${e.message}</p>`;console.error('Fetch/Render Error:',e)})</script>"
    "</body></html>";

// --- Embedded SSR HTML Template (Added Header) ---
/**
 * @brief HTML template for server-side rendering mode
 * 
 * Template used to wrap rendered HTML content in backend mode.
 * Contains a %s placeholder where the rendered HTML fragment is inserted.
 * Includes CSS styles and the top navigation header with home link.
 */
const char *ssr_html_template =
    "<!doctype html><html><head><meta charset=\"utf-8\"/><title>C Markdown Server (SSR)</title>"
    "<style>"
    "body{font-family:sans-serif;line-height:1.6;padding:20px;max-width:800px;margin:auto;padding-top:50px;position:relative;}" // Added padding-top and relative position
    ".top-header{position:absolute;top:10px;left:20px;font-size:0.9em;}"                                                        // Style for header
    ".top-header a{text-decoration:none;color:#007bff;}"
    ".top-header a:hover{text-decoration:underline;}"
    "h1,h2,h3,h4,h5,h6{margin-top:1.5em;margin-bottom:.5em;border-bottom:1px solid #eee;padding-bottom:.2em}h1{border-bottom-width:2px}"
    "p{margin-bottom:1em}"
    "code{background-color:#f0f0f0;padding:.2em .4em;border-radius:3px;font-family:monospace}"
    "pre{background-color:#f8f8f8;border:1px solid #ddd;padding:1em;overflow:auto;border-radius:4px}"
    "pre code{background-color:transparent;padding:0;border-radius:0}"
    "strong{font-weight:bold}em{font-style:italic}hr{border:0;border-top:1px solid #ccc;margin:2em 0}"
    "ul,ol{padding-left:2em;margin-bottom:1em}li{margin-bottom:.3em}"
    "</style></head>"
    "<body>"
    "<div class=\"top-header\"><a href=\"/\">← Home</a></div>" // Added Header Link
    "%s"                                                       // Placeholder for the rendered HTML content
    "</body></html>";

// --- Forward Declarations ---
void handle_connection(socket_t client_socket);
void send_response(socket_t sock, const char *status, const char *content_type, const char *body, long body_len);
void send_file_response(socket_t sock, const char *filename, const char *content_type);
void send_error_response(socket_t sock, int status_code, const char *status_message, const char *body_message);
bool file_exists_and_is_regular(const char *filename);
bool is_path_safe(const char *relative_path);
long get_file_size(const char *filename);
bool ensure_directory_exists(const char *filepath);
char *render_markdown_to_html(const char *markdown);
char *render_inline_markdown(const char *text);
char *safe_append(char *dest, size_t *dest_len, size_t *dest_cap, const char *src, size_t src_len);
char *escape_html(const char *text);
void print_usage(const char *prog_name);
void sigint_handler(int sig);

// --- Global Variables ---
/**
 * @brief Flag controlling the main server loop
 * 
 * When true, the server continues accepting connections.
 * Set to false by the SIGINT handler to initiate graceful shutdown.
 */
volatile sig_atomic_t keep_running = 1;

// --- Signal Handler ---
/**
 * @brief Handles SIGINT signal (Ctrl+C) to perform graceful server shutdown
 * 
 * This function is registered with the signal handler and called when a SIGINT is
 * received. It uses write() which is signal-safe (unlike printf) to inform the user
 * and sets the global keep_running flag to 0 to terminate the main accept loop.
 * 
 * @param sig The signal number (unused but required by signal handler signature)
 */
void sigint_handler(int sig)
{
    (void)sig;
    const char msg[] = "\nCaught SIGINT, shutting down...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1); // Use write for better signal safety
    keep_running = 0;
}

// --- Main Function ---
/**
 * @brief Main entry point for the Markdown server
 * 
 * Parses command-line arguments, initializes the server socket, and enters the main
 * connection accept loop. Supports configuration of render mode (frontend/backend),
 * base directory, IP address, port, and verbosity level. Sets up signal handling for
 * graceful shutdown. Handles platform-specific socket initialization for both POSIX
 * and Windows.
 * 
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return int Exit code (0 for success, non-zero for errors)
 */
int main(int argc, char *argv[])
{
    // --- Parse Command Line Arguments ---
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0)
        {
            if (G_verbose_level < 3)
                G_verbose_level = 1;
        }
        else if (strcmp(argv[i], "-vv") == 0)
        {
            if (G_verbose_level < 3)
                G_verbose_level = 2;
        }
        else if (strcmp(argv[i], "-vvv") == 0)
        {
            G_verbose_level = 3;
        }
        else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc)
        {
            i++;
            if (strcmp(argv[i], "frontend") == 0)
                G_render_mode = RENDER_FRONTEND;
            else if (strcmp(argv[i], "backend") == 0)
                G_render_mode = RENDER_BACKEND;
            else
            {
                LOG_ERROR("Invalid value for --render: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc)
        {
            G_base_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc)
        {
            G_listen_ip_str = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            char *endptr;
            long port_l = strtol(argv[++i], &endptr, 10);
            if (*endptr != '\0' || port_l <= 0 || port_l > 65535)
            {
                LOG_ERROR("Invalid port number: %s\n", argv[i]);
                return 1;
            }
            G_listen_port = (uint16_t)port_l;
        }
        else
        {
            LOG_ERROR("Unknown or incomplete argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate Base Directory
    struct stat dir_stat;
    if (stat(G_base_dir, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode))
    {
        LOG_ERROR("Invalid base directory '%s': %s\n", G_base_dir, strerror(errno));
        return 1;
    }
    LOG_DEBUG("Base directory '%s' validated.\n", G_base_dir);

// Platform Init & Signal Handling
#ifdef _WIN32
    WSADATA wsaData;
    int wsa_res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_res != 0)
    {
        LOG_ERROR("WSAStartup failed: %d\n", wsa_res);
        return 1;
    }
#endif
    signal(SIGINT, sigint_handler);

    // Socket Setup
    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
    {
        perror("[ERROR] Socket creation failed"); /* cleanup */
        return 1;
    }
    LOG_DEBUG("Socket created (fd=" SOCKET_FMT ")\n", SOCKET_CAST(server_fd));

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    // Prepare Socket Address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(G_listen_port);
#ifdef _WIN32
    server_addr.sin_addr.s_addr = inet_addr(G_listen_ip_str); // Deprecated but simple for demo
    if (server_addr.sin_addr.s_addr == INADDR_NONE)
    {
        LOG_ERROR("Invalid IP address format (inet_addr): %s\n", G_listen_ip_str);
        CLOSE_SOCKET(server_fd);
        WSACleanup();
        return 1;
    }
#else
    if (inet_pton(AF_INET, G_listen_ip_str, &server_addr.sin_addr) <= 0)
    {
        LOG_ERROR("Invalid IP address format (inet_pton): %s\n", G_listen_ip_str);
        CLOSE_SOCKET(server_fd);
        return 1;
    }
#endif

    // Bind & Listen
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        perror("[ERROR] Socket bind failed");
        fprintf(stderr, "[ERROR] Attempted to bind to %s:%u\n", G_listen_ip_str, G_listen_port);
        CLOSE_SOCKET(server_fd); /* cleanup */
        return 1;
    }
    if (listen(server_fd, MAX_CONNECTIONS) == SOCKET_ERROR)
    {
        perror("[ERROR] Listen failed");
        CLOSE_SOCKET(server_fd); /* cleanup */
        return 1;
    }

    printf("Server configuration:\n");
    printf("  Render Mode: %s\n", (G_render_mode == RENDER_FRONTEND) ? "Frontend (Client-Side)" : "Backend (Server-Side)");
    printf("  Base Dir:    %s\n", G_base_dir);
    printf("  Verbosity:   %d\n", G_verbose_level);
    printf("Server listening on http://%s:%u\n", G_listen_ip_str, G_listen_port);

    // Accept Loop
    while (keep_running)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        socket_t client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (!keep_running)
        {
            if (client_socket != INVALID_SOCKET)
                CLOSE_SOCKET(client_socket);
            break;
        }
        if (client_socket == INVALID_SOCKET)
        {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR)
                continue;
#else
            if (errno == EINTR)
                continue;
#endif
            perror("[WARN] Accept failed");
            continue;
        }
        LOG_INFO("Connection accepted (socket=" SOCKET_FMT ")\n", SOCKET_CAST(client_socket));

        handle_connection(client_socket);
        CLOSE_SOCKET(client_socket);
        LOG_INFO("Connection closed (socket=" SOCKET_FMT ")\n", SOCKET_CAST(client_socket));
    }

    // Cleanup
    printf("Shutting down server socket.\n");
    CLOSE_SOCKET(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    printf("Server shut down gracefully.\n");
    return 0;
}

// --- Request Handler (Corrected Page Combination) ---
/**
 * @brief Processes an individual client connection
 * 
 * This function handles a client's HTTP request by:
 * 1. Reading and parsing the request to extract the HTTP method and URI
 * 2. Validating the request (only GET method is supported)
 * 3. Normalizing and checking the safety of the requested path
 * 4. Branching based on render mode (frontend or backend):
 *    - Frontend mode: Serves either raw .md files or the embedded HTML shell
 *    - Backend mode: Reads the .md file, renders it to HTML, and sends the complete page
 * 5. Handling error cases and special cases (like missing index.md)
 * 
 * @param client_socket Socket descriptor for the connected client
 */
void handle_connection(socket_t client_socket)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_received = -1;
#ifdef _WIN32
    bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
#else
    bytes_received = read(client_socket, buffer, BUFFER_SIZE - 1);
#endif
    if (bytes_received <= 0)
    { /* Handle read error/disconnect */
        return;
    }
    buffer[bytes_received] = '\0';
    LOG_DETAIL("Received Request (%ld bytes) from socket " SOCKET_FMT ":\n---\n%s\n---\n", (long)bytes_received, SOCKET_CAST(client_socket), buffer);

    char method[16], request_uri[MAX_PATH_LEN];
    if (sscanf(buffer, "%15s %511s %*s", method, request_uri) < 2)
    {
        send_error_response(client_socket, 400, "BR", "Parse fail");
        return;
    }
    if (strcmp(method, "GET") != 0)
    {
        send_error_response(client_socket, 501, "NI", "GET only");
        return;
    }
    LOG_INFO("Request from " SOCKET_FMT ": %s %s\n", SOCKET_CAST(client_socket), method, request_uri);

    // Path Processing
    char *query_start = strchr(request_uri, '?');
    if (query_start)
        *query_start = '\0';
    char normalized_req_path[MAX_PATH_LEN];
    strncpy(normalized_req_path, request_uri, sizeof(normalized_req_path) - 1);
    normalized_req_path[sizeof(normalized_req_path) - 1] = '\0';
    for (char *p = normalized_req_path; *p; ++p)
    {
        if (*p == '/')
            *p = PATH_SEPARATOR;
    }
    const char *relative_path = (normalized_req_path[0] == PATH_SEPARATOR) ? normalized_req_path + 1 : normalized_req_path;
    if (strcmp(relative_path, "") == 0 || strcmp(relative_path, ".") == 0)
        relative_path = "index";
    LOG_DEBUG("Normalized relative path: '%s'\n", relative_path);
    if (!is_path_safe(relative_path))
    {
        send_error_response(client_socket, 400, "BR", "Unsafe path");
        return;
    }

    // Branch based on Render Mode
    if (G_render_mode == RENDER_FRONTEND)
    {
        // --- Frontend Mode ---
        bool serve_markdown_file = false;
        char target_filepath[MAX_PATH_LEN + strlen(G_base_dir) + 5];
        size_t req_len = strlen(relative_path);
        if (req_len > 3 && strcasecmp(relative_path + req_len - 3, ".md") == 0)
        {
            serve_markdown_file = true;
            snprintf(target_filepath, sizeof(target_filepath), "%s%c%s", G_base_dir, PATH_SEPARATOR, relative_path);
        }
        if (serve_markdown_file)
        {
            LOG_DETAIL("Frontend Mode: Serving raw MD file: %s\n", target_filepath);
            if (!ensure_directory_exists(target_filepath))
            {
                send_error_response(client_socket, 500, "ISE", "Dir check error");
                return;
            }
            if (file_exists_and_is_regular(target_filepath))
            {
                send_file_response(client_socket, target_filepath, "text/markdown; charset=utf-8");
            }
            else
            {
                char target_index_path[sizeof(target_filepath)];
                snprintf(target_index_path, sizeof(target_index_path), "%s%cindex.md", G_base_dir, PATH_SEPARATOR);
                if (strcmp(target_filepath, target_index_path) == 0 && errno == ENOENT)
                {
                    const char *m = "# Welcome!\n\n`index.md` missing.";
                    send_response(client_socket, "200 OK", "text/md", m, strlen(m));
                }
                else
                {
                    send_error_response(client_socket, 404, "NF", "MD not found.");
                }
            }
        }
        else
        {
            LOG_DETAIL("Frontend Mode: Serving embedded HTML shell for request '%s'\n", request_uri);
            send_response(client_socket, "200 OK", "text/html", html_index_content, strlen(html_index_content));
        }
    }
    else
    {
        // --- Backend Mode ---
        char target_md_filepath[MAX_PATH_LEN + strlen(G_base_dir) + 5];
        size_t rel_len = strlen(relative_path);
        if (rel_len > 3 && strcasecmp(relative_path + rel_len - 3, ".md") == 0)
        {
            snprintf(target_md_filepath, sizeof(target_md_filepath), "%s%c%s", G_base_dir, PATH_SEPARATOR, relative_path);
        }
        else
        {
            snprintf(target_md_filepath, sizeof(target_md_filepath), "%s%c%s.md", G_base_dir, PATH_SEPARATOR, relative_path);
        }
        LOG_DETAIL("Backend Mode: Attempting to render: %s\n", target_md_filepath);

        if (!ensure_directory_exists(target_md_filepath))
        {
            send_error_response(client_socket, 500, "ISE", "Dir check error");
            return;
        }
        if (file_exists_and_is_regular(target_md_filepath))
        {
            long file_size = get_file_size(target_md_filepath);
            if (file_size < 0)
            {
                send_error_response(client_socket, 500, "ISE", "Size check fail");
                return;
            }
            char *md_content = NULL;
            if (file_size > 0)
            {
                md_content = (char *)malloc(file_size + 1);
                if (!md_content)
                {
                    send_error_response(client_socket, 500, "ISE", "Mem fail");
                    return;
                }
                FILE *file = fopen(target_md_filepath, "rb");
                if (!file)
                {
                    free(md_content);
                    send_error_response(client_socket, 500, "ISE", "Open fail");
                    return;
                }
                if (fread(md_content, 1, file_size, file) != (size_t)file_size)
                {
                    fclose(file);
                    free(md_content);
                    send_error_response(client_socket, 500, "ISE", "Read fail");
                    return;
                }
                fclose(file);
                md_content[file_size] = '\0';
            }
            else
            {
                md_content = strdup("");
            }
            if (!md_content)
            {
                send_error_response(client_socket, 500, "ISE", "Content error");
                return;
            }

            char *rendered_html_fragment = render_markdown_to_html(md_content);
            free(md_content);
            if (!rendered_html_fragment)
            {
                send_error_response(client_socket, 500, "ISE", "Render fail");
                return;
            }

            // --- FIXED PAGE COMBINATION using snprintf ---
            int required_size = snprintf(NULL, 0, ssr_html_template, rendered_html_fragment);
            if (required_size < 0)
            {
                LOG_ERROR("snprintf size calculation failed\n");
                free(rendered_html_fragment);
                send_error_response(client_socket, 500, "ISE", "Page gen error (size calc)");
                return;
            }
            size_t full_page_size = (size_t)required_size + 1; // +1 for null terminator
            char *full_page = (char *)malloc(full_page_size);
            if (!full_page)
            {
                LOG_ERROR("Malloc failed for full page (%zu bytes)\n", full_page_size);
                free(rendered_html_fragment);
                send_error_response(client_socket, 500, "ISE", "Page mem fail");
                return;
            }
            int written_size = snprintf(full_page, full_page_size, ssr_html_template, rendered_html_fragment);
            free(rendered_html_fragment); // Free fragment now
            if (written_size < 0 || (size_t)written_size >= full_page_size)
            {
                LOG_ERROR("snprintf failed during page combination (written %d, size %zu)\n", written_size, full_page_size);
                free(full_page);
                send_error_response(client_socket, 500, "ISE", "Page gen error (combine)");
                return;
            }
            // --- End of fix ---

            LOG_DETAIL("Backend Mode: Sending rendered HTML page (%zu bytes)\n", strlen(full_page));
            send_response(client_socket, "200 OK", "text/html; charset=utf-8", full_page, strlen(full_page));
            free(full_page);
        }
        else
        { /* Handle 404 / default index.md message for backend */
            int err = errno;
            char target_index_path[sizeof(target_md_filepath)];
            snprintf(target_index_path, sizeof(target_index_path), "%s%cindex.md", G_base_dir, PATH_SEPARATOR);
            if (strcmp(target_md_filepath, target_index_path) == 0 && err == ENOENT)
            {
                const char *m = "# Welcome!\n\n`index.md` missing.";
                char *r = render_markdown_to_html(m);
                if (r)
                {
                    // --- FIXED PAGE COMBINATION for default msg ---
                    int req_size_def = snprintf(NULL, 0, ssr_html_template, r);
                    if (req_size_def >= 0)
                    {
                        size_t fp_size = (size_t)req_size_def + 1;
                        char *fp = (char *)malloc(fp_size);
                        if (fp)
                        {
                            if (snprintf(fp, fp_size, ssr_html_template, r) > 0)
                            {
                                send_response(client_socket, "200 OK", "text/html", fp, strlen(fp));
                            }
                            else
                            {
                                LOG_ERROR("snprintf default combine failed\n");
                            }
                            free(fp);
                        }
                        else
                        {
                            LOG_ERROR("Malloc failed for default page\n");
                        }
                    }
                    else
                    {
                        LOG_ERROR("snprintf default size calc failed\n");
                    }
                    // --- End of fix ---
                    free(r);
                }
                else
                {
                    send_error_response(client_socket, 500, "ISE", "Render fail");
                }
            }
            else
            {
                send_error_response(client_socket, 404, "NF", "Page source not found.");
            }
        }
    }
}
// --- Utility: Safe String Appending ---
/**
 * @brief Safely appends a string to a dynamically allocated buffer
 * 
 * This utility function handles dynamic memory allocation and growth for string
 * building operations. If the destination buffer isn't large enough to accommodate
 * the source string, it automatically reallocates with a larger capacity. This is
 * particularly useful for the Markdown renderer which builds HTML incrementally.
 * 
 * @param dest The destination buffer (may be reallocated if needed)
 * @param dest_len Pointer to the current length of content in dest
 * @param dest_cap Pointer to the current capacity of dest
 * @param src Source string to append
 * @param src_len Length of the source string
 * @return char* Updated destination buffer pointer (may differ from input if reallocated)
 *         or NULL on allocation failure (original dest is freed in this case)
 */
char *safe_append(char *dest, size_t *dest_len, size_t *dest_cap, const char *src, size_t src_len)
{ /* ... as before ... */
    if (*dest_len + src_len + 1 > *dest_cap)
    {
        size_t new_cap = (*dest_cap == 0) ? 256 : (*dest_cap * 2);
        while (*dest_len + src_len + 1 > new_cap)
        {
            new_cap *= 2;
        }
        LOG_DEBUG("Reallocating buffer from %zu to %zu\n", *dest_cap, new_cap);
        char *new_dest = (char *)realloc(dest, new_cap);
        if (!new_dest)
        {
            LOG_ERROR("realloc failed in safe_append\n");
            free(dest);
            return NULL;
        }
        dest = new_dest;
        *dest_cap = new_cap;
    }
    memcpy(dest + *dest_len, src, src_len);
    *dest_len += src_len;
    dest[*dest_len] = '\0';
    return dest;
}

// --- Utility: HTML Escaping (Corrected) ---
/**
 * @brief Escapes special HTML characters to prevent injection issues
 * 
 * Converts characters that have special meaning in HTML to their corresponding
 * entity references to ensure they display correctly and prevent injection attacks:
 * - < becomes &lt;
 * - > becomes &gt;
 * - & becomes &amp;
 * - " becomes &quot;
 * 
 * Uses safe_append for dynamic memory management during conversion.
 * 
 * @param text Input text to escape
 * @return char* Newly allocated string containing the escaped content,
 *         or NULL on allocation failure
 */
char *escape_html(const char *text)
{
    if (!text)
        return NULL;
    size_t text_len = strlen(text);
    // Estimate capacity: start with text length, add potential padding for escapes
    // Each escape adds 3-5 extra chars. Assume ~10% might be escaped on average?
    size_t cap = text_len + (text_len / 10) + 32; // Add some base padding too
    char *escaped = (char *)malloc(cap);
    if (!escaped)
    {
        LOG_ERROR("Malloc failed in escape_html (initial size %zu)\n", cap);
        return NULL;
    }
    escaped[0] = '\0';
    size_t len = 0;

    for (const char *p = text; *p; ++p)
    {
        const char *replacement = NULL;
        size_t rep_len = 0;

        // Use the correct HTML entity string literals
        switch (*p)
        {
        case '<':
            replacement = "&lt;";
            rep_len = 4;
            break;
        case '>':
            replacement = "&gt;";
            rep_len = 4;
            break;
        case '&':
            replacement = "&amp;";
            rep_len = 5;
            break;
        case '"':
            replacement = "&quot;";
            rep_len = 6;
            break;
            // Add ' for single quotes if needed, though less critical
            // case '\'': replacement = "'"; rep_len = 6; break;
        }

        if (replacement)
        {
            // Append the HTML entity string
            escaped = safe_append(escaped, &len, &cap, replacement, rep_len);
            if (!escaped)
                return NULL; // safe_append handles free on error
        }
        else
        {
            // Append the original character (no replacement needed)
            escaped = safe_append(escaped, &len, &cap, p, 1);
            if (!escaped)
                return NULL;
        }
    }
    // Optional: Shrink buffer to actual size if memory is critical
    // char* final_escaped = realloc(escaped, len + 1);
    // return final_escaped ? final_escaped : escaped; // Return original if realloc fails
    return escaped;
}

// --- Inline Markdown Renderer (Corrected Code Span) ---
/**
 * @brief Renders inline Markdown elements to HTML
 * 
 * Processes inline Markdown syntax including:
 * - Bold (**text** or __text__)
 * - Italic (*text* or _text_)
 * - Code spans (`code`)
 * - Links ([text](url))
 * - Escaped characters (backslash followed by a special character)
 * 
 * Uses a character-by-character parsing approach with markers and state tracking.
 * HTML-escapes content appropriately to prevent injection issues.
 * 
 * @param text Input Markdown text to render
 * @return char* Newly allocated string containing rendered HTML,
 *         or NULL on allocation failure
 */
char *render_inline_markdown(const char *text)
{
    if (!text)
        return NULL;
    LOG_DEBUG("Rendering inline: '%.30s...'\n", text);

    size_t text_len = strlen(text);
    size_t html_cap = text_len + 256;
    char *html = (char *)malloc(html_cap);
    if (!html)
    {
        LOG_ERROR("Malloc failed in render_inline\n");
        return NULL;
    }
    html[0] = '\0';
    size_t html_len = 0;

    const char *p = text;
    const char *last_copy_pos = text;

    while (*p)
    {
        const char *marker_pos = p;
        char open_tag[64] = {0};
        char close_tag[16] = {0};
        const char *end_marker = NULL;
        const char *content_start = NULL;
        size_t content_len = 0;
        bool found_inline = false;
        bool escape_content = false;

        // Escape Character
        if (*p == '\\' && p[1] != '\0' && strchr("*_[]()`\\", p[1]))
        {
            if (marker_pos > last_copy_pos)
            {
                html = safe_append(html, &html_len, &html_cap, last_copy_pos, marker_pos - last_copy_pos);
                if (!html)
                    return NULL;
            }
            html = safe_append(html, &html_len, &html_cap, p + 1, 1);
            if (!html)
                return NULL; // Append escaped char
            p += 2;
            last_copy_pos = p;
            continue;
        }
        // Single Backtick Code Spans
        else if (*p == '`')
        {                                    // LOOK FOR SINGLE BACKTICK
            end_marker = strchr(p + 1, '`'); // Find next single backtick
            if (end_marker)
            {
                strcpy(open_tag, "<code>");
                strcpy(close_tag, "</code>");
                content_start = p + 1;
                content_len = end_marker - content_start;
                // Basic trim space inside `code`
                while (content_len > 0 && isspace((unsigned char)*content_start))
                {
                    content_start++;
                    content_len--;
                }
                while (content_len > 0 && isspace((unsigned char)*(content_start + content_len - 1)))
                {
                    content_len--;
                }
                p = end_marker + 1; // Move past closing backtick
                found_inline = true;
                escape_content = true;
                LOG_DEBUG("Found code span, len %zu\n", content_len);
            } // else: no closing backtick found, treat as literal
        }
        // Bold/Italic
        else if (strncmp(p, "**", 2) == 0 || strncmp(p, "__", 2) == 0)
        {
            char marker[3] = {*p, *p, '\0'};
            end_marker = strstr(p + 2, marker);
            if (end_marker && end_marker > p + 2)
            {
                strcpy(open_tag, "<strong>");
                strcpy(close_tag, "</strong>");
                content_start = p + 2;
                content_len = end_marker - content_start;
                p = end_marker + 2;
                found_inline = true;
            }
        }
        else if ((*p == '*' || *p == '_'))
        {
            char marker[2] = {*p, '\0'};
            end_marker = strstr(p + 1, marker);
            if (end_marker && end_marker > p + 1)
            {
                bool start_ok = (p == text || !isalnum((unsigned char)*(p - 1)));
                bool end_ok = (end_marker[1] == '\0' || !isalnum((unsigned char)end_marker[1]));
                bool no_internal_space = !isspace((unsigned char)*(p + 1)) && !isspace((unsigned char)*(end_marker - 1));
                if (start_ok && end_ok && no_internal_space)
                {
                    strcpy(open_tag, "<em>");
                    strcpy(close_tag, "</em>");
                    content_start = p + 1;
                    content_len = end_marker - content_start;
                    p = end_marker + 1;
                    found_inline = true;
                }
            }
        }
        // Links
        else if (*p == '[')
        {
            const char *text_end = strchr(p + 1, ']');
            if (text_end && text_end[1] == '(')
            {
                const char *url_start = text_end + 2;
                const char *url_end = url_start;
                int paren_level = 1;
                while (*url_end)
                {
                    if (*url_end == '(')
                        paren_level++;
                    else if (*url_end == ')')
                    {
                        if (--paren_level == 0)
                            break;
                    }
                    url_end++;
                }
                if (*url_end == ')')
                {
                    content_start = p + 1;
                    content_len = text_end - content_start;
                    size_t url_len = url_end - url_start;
                    char *link_url_raw = (char *)malloc(url_len + 1);
                    if (!link_url_raw)
                    {
                        free(html);
                        return NULL;
                    }
                    strncpy(link_url_raw, url_start, url_len);
                    link_url_raw[url_len] = '\0';
                    char *escaped_url_attr = escape_html(link_url_raw);
                    free(link_url_raw);
                    if (!escaped_url_attr)
                    {
                        free(html);
                        return NULL;
                    }
                    snprintf(open_tag, sizeof(open_tag), "<a href=\"%s\">", escaped_url_attr);
                    strcpy(close_tag, "</a>");
                    free(escaped_url_attr);
                    p = url_end + 1;
                    found_inline = true;
                }
            }
        }

        // Process Found Inline
        if (found_inline)
        {
            // Append text before marker
            if (marker_pos > last_copy_pos)
            {
                html = safe_append(html, &html_len, &html_cap, last_copy_pos, marker_pos - last_copy_pos);
                if (!html)
                    return NULL;
            }
            // Extract content
            char *content_text = (char *)malloc(content_len + 1);
            if (!content_text)
            {
                free(html);
                return NULL;
            }
            strncpy(content_text, content_start, content_len);
            content_text[content_len] = '\0';
            // Render content (escape or recursive - using escape for demo)
            char *rendered_content = escape_content ? escape_html(content_text) : escape_html(content_text); // render_inline_markdown(content_text);
            free(content_text);
            if (!rendered_content)
            {
                free(html);
                return NULL;
            }
            // Append tags and content
            size_t open_tag_len = strlen(open_tag);
            size_t rendered_content_len = strlen(rendered_content);
            size_t close_tag_len = strlen(close_tag);
            html = safe_append(html, &html_len, &html_cap, open_tag, open_tag_len);
            if (!html)
            {
                free(rendered_content);
                return NULL;
            }
            html = safe_append(html, &html_len, &html_cap, rendered_content, rendered_content_len);
            free(rendered_content);
            if (!html)
                return NULL;
            html = safe_append(html, &html_len, &html_cap, close_tag, close_tag_len);
            if (!html)
                return NULL;
            last_copy_pos = p; // Update position
        }
        else
        {
            p++;
        } // No marker found, advance
    }

    // Append remaining text
    if (p > last_copy_pos)
    {
        html = safe_append(html, &html_len, &html_cap, last_copy_pos, p - last_copy_pos);
        if (!html)
            return NULL;
    }

    LOG_DEBUG("Finished inline render, final length %zu\n", html_len);
    return html;
}

// --- Main Markdown Renderer (Handles Blocks - Corrected Code Block Logic v2) ---
/**
 * @brief Renders complete Markdown document to HTML
 * 
 * Processes Markdown block-level and inline elements, including:
 * - Headings (# to ######)
 * - Paragraphs
 * - Unordered lists (* + -)
 * - Ordered lists (1. 2. etc)
 * - Horizontal rules (---, ***, ___)
 * - Fenced code blocks (```)
 * - All inline elements via render_inline_markdown()
 * 
 * Maintains state to properly open/close HTML tags, manages nesting of elements,
 * and handles special cases like consecutive list items. Uses dynamic memory
 * allocation via safe_append to build the output HTML.
 * 
 * @param markdown Input Markdown text to render
 * @return char* Newly allocated string containing complete rendered HTML,
 *         or NULL on allocation failure
 */
char *render_markdown_to_html(const char *markdown)
{
    if (!markdown)
        return NULL;
    LOG_DEBUG("Starting full markdown render\n");
    size_t md_len = strlen(markdown);
    size_t html_cap = md_len + 1024; // Initial estimate
    char *html = (char *)malloc(html_cap);
    if (!html)
    {
        LOG_ERROR("Initial malloc failed in render_markdown_to_html\n");
        return NULL;
    }
    html[0] = '\0';
    size_t html_len = 0;

    const char *p = markdown;
    const char *end = markdown + md_len;
    bool in_p = false, in_pre = false, in_ul = false, in_ol = false;

    while (p < end)
    {
        const char *line_start = p;
        const char *line_end = strchr(p, '\n');
        if (!line_end)
            line_end = end;
        size_t line_len = line_end - line_start;

        LOG_DEBUG("Processing line (in_pre=%d): '%.*s'\n", in_pre, (int)line_len, line_start);

        // --- Handle INSIDE a pre block ---
        // This check MUST come first and prevent other parsing if true.
        if (in_pre)
        {
            // Check if this line is the closing fence
            // Allow optional whitespace before/after fence
            const char *trimmed_line_start = line_start;
            size_t trimmed_line_len = line_len;
            while (trimmed_line_len > 0 && isspace((unsigned char)*trimmed_line_start))
            {
                trimmed_line_start++;
                trimmed_line_len--;
            }
            while (trimmed_line_len > 0 && isspace((unsigned char)*(trimmed_line_start + trimmed_line_len - 1)))
            {
                trimmed_line_len--;
            }

            if (trimmed_line_len == 3 && strncmp(trimmed_line_start, "```", 3) == 0)
            {
                LOG_DEBUG("Found closing code block fence\n");
                html = safe_append(html, &html_len, &html_cap, "</code></pre>\n", 14);
                if (!html)
                    return NULL;
                in_pre = false;
            }
            else
            {
                // It's content inside the pre block, escape it
                // Need to handle the original line_len here, not trimmed length
                char *current_line_text = (char *)malloc(line_len + 1);
                if (!current_line_text)
                {
                    free(html);
                    return NULL;
                }
                strncpy(current_line_text, line_start, line_len);
                current_line_text[line_len] = '\0';
                char *escaped_line = escape_html(current_line_text);
                free(current_line_text);
                if (!escaped_line)
                {
                    free(html);
                    return NULL;
                }
                // Append escaped line and a newline
                html = safe_append(html, &html_len, &html_cap, escaped_line, strlen(escaped_line));
                if (!html)
                {
                    free(escaped_line);
                    return NULL;
                }
                html = safe_append(html, &html_len, &html_cap, "\n", 1);
                free(escaped_line);
                if (!html)
                    return NULL;
                LOG_DEBUG("Appended escaped line inside pre block\n");
            }
        }
        // --- Handle Block Starts and other elements (if NOT inside a pre block) ---
        else
        {
            // Trim leading/trailing whitespace for block detection
            size_t l_space = 0;
            while (l_space < line_len && isspace((unsigned char)line_start[l_space]))
                l_space++;
            const char *c_start = line_start + l_space;
            size_t c_len = line_len - l_space;
            while (c_len > 0 && isspace((unsigned char)*(c_start + c_len - 1)))
                c_len--;

            // Check for STARTING fence ```
            if (c_len >= 3 && strncmp(c_start, "```", 3) == 0)
            {
                LOG_DEBUG("Found opening code block fence\n");
                // Close any open paragraph or list first
                if (in_p)
                {
                    html = safe_append(html, &html_len, &html_cap, "</p>\n", 5);
                    in_p = false;
                }
                if (!html)
                    return NULL;
                if (in_ul)
                {
                    html = safe_append(html, &html_len, &html_cap, "</ul>\n", 6);
                    in_ul = false;
                }
                if (!html)
                    return NULL;
                if (in_ol)
                {
                    html = safe_append(html, &html_len, &html_cap, "</ol>\n", 6);
                    in_ol = false;
                }
                if (!html)
                    return NULL;
                // Open the pre block
                html = safe_append(html, &html_len, &html_cap, "<pre><code>", 11);
                if (!html)
                    return NULL;
                in_pre = true;
            }
            else
            {
                // Process other block elements only if not starting ```
                bool is_ul = false, is_ol = false;
                const char *i_start = c_start;
                size_t i_len = c_len;
                // List Detection...
                if (c_len >= 1 && strchr("*+-", c_start[0]) && (c_len == 1 || isspace((unsigned char)c_start[1])))
                {
                    is_ul = true;
                    i_start = c_start + 1;
                    while (i_start < c_start + c_len && isspace((unsigned char)*i_start))
                        i_start++;
                    i_len = c_start + c_len - i_start;
                    LOG_DEBUG("Detected UL item\n");
                } // Handle leading space
                else if (c_len >= 2 && isdigit((unsigned char)c_start[0]))
                {
                    char *ep;
                    long n = strtol(c_start, &ep, 10);
                    if (n > 0 && ep > c_start && *ep == '.' && isspace((unsigned char)*(ep + 1)))
                    {
                        is_ol = true;
                        i_start = ep + 1;
                        while (isspace((unsigned char)*i_start))
                            i_start++;
                        i_len = c_start + c_len - i_start;
                        LOG_DEBUG("Detected OL item\n");
                    }
                }

                // State Transitions... (closing lists/paragraphs)
                if (!is_ul && !is_ol)
                {
                    if (in_ul)
                    {
                        html = safe_append(html, &html_len, &html_cap, "</ul>\n", 6);
                        in_ul = false;
                        LOG_DEBUG("Closed UL\n");
                    }
                    if (!html)
                        return NULL;
                    if (in_ol)
                    {
                        html = safe_append(html, &html_len, &html_cap, "</ol>\n", 6);
                        in_ol = false;
                        LOG_DEBUG("Closed OL\n");
                    }
                    if (!html)
                        return NULL;
                }
                if (is_ul && in_ol)
                {
                    html = safe_append(html, &html_len, &html_cap, "</ol>\n", 6);
                    in_ol = false;
                    LOG_DEBUG("Closed OL before UL\n");
                }
                if (!html)
                    return NULL;
                if (is_ol && in_ul)
                {
                    html = safe_append(html, &html_len, &html_cap, "</ul>\n", 6);
                    in_ul = false;
                    LOG_DEBUG("Closed UL before OL\n");
                }
                if (!html)
                    return NULL;
                if ((is_ul || is_ol) && in_p)
                {
                    html = safe_append(html, &html_len, &html_cap, "</p>\n", 5);
                    in_p = false;
                    LOG_DEBUG("Closed paragraph before list\n");
                }
                if (!html)
                    return NULL;

                // Process Line Content...
                if (c_len == 0)
                {
                    if (in_p)
                    {
                        html = safe_append(html, &html_len, &html_cap, "</p>\n", 5);
                        in_p = false;
                        LOG_DEBUG("Closed paragraph on blank line\n");
                    }
                    if (!html)
                        return NULL;
                } // Blank line
                // List Items...
                else if (is_ul || is_ol)
                {
                    if (is_ul && !in_ul)
                    {
                        html = safe_append(html, &html_len, &html_cap, "<ul>\n", 5);
                        in_ul = true;
                        LOG_DEBUG("Opened UL\n");
                    }
                    if (!html)
                        return NULL;
                    if (is_ol && !in_ol)
                    {
                        html = safe_append(html, &html_len, &html_cap, "<ol>\n", 5);
                        in_ol = true;
                        LOG_DEBUG("Opened OL\n");
                    }
                    if (!html)
                        return NULL;
                    html = safe_append(html, &html_len, &html_cap, "<li>", 4);
                    if (!html)
                        return NULL;
                    // Use i_start and i_len calculated during detection
                    char *it = (char *)malloc(i_len + 1);
                    if (!it)
                    {
                        free(html);
                        return NULL;
                    }
                    strncpy(it, i_start, i_len);
                    it[i_len] = '\0';
                    char *ri = render_inline_markdown(it);
                    free(it);
                    if (!ri)
                    {
                        free(html);
                        return NULL;
                    }
                    html = safe_append(html, &html_len, &html_cap, ri, strlen(ri));
                    free(ri);
                    if (!html)
                        return NULL;
                    html = safe_append(html, &html_len, &html_cap, "</li>\n", 6);
                    if (!html)
                        return NULL;
                }
                // Headings...
                else if (c_start[0] == '#')
                {
                    if (in_p)
                    {
                        html = safe_append(html, &html_len, &html_cap, "</p>\n", 5);
                        in_p = false;
                        LOG_DEBUG("Closed paragraph before heading\n");
                    }
                    if (!html)
                        return NULL;
                    size_t level = 0;
                    while (level < c_len && c_start[level] == '#')
                        level++;
                    if (level > 0 && level <= 6 && (level == c_len || isspace((unsigned char)c_start[level])))
                    {
                        LOG_DEBUG("Detected H%zu\n", level);
                        const char *t_start = c_start + level;
                        while (t_start < c_start + c_len && isspace((unsigned char)*t_start))
                            t_start++;
                        size_t t_len = c_start + c_len - t_start;
                        while (t_len > 0 && (isspace((unsigned char)*(t_start + t_len - 1)) || *(t_start + t_len - 1) == '#'))
                            t_len--;
                        char *ht = (char *)malloc(t_len + 1);
                        if (!ht)
                        {
                            free(html);
                            return NULL;
                        }
                        strncpy(ht, t_start, t_len);
                        ht[t_len] = '\0';
                        char *ri = render_inline_markdown(ht);
                        free(ht);
                        if (!ri)
                        {
                            free(html);
                            return NULL;
                        }
                        char tag[8];
                        snprintf(tag, sizeof(tag), "<h%zu>", level);
                        html = safe_append(html, &html_len, &html_cap, tag, strlen(tag));
                        if (!html)
                        {
                            free(ri);
                            return NULL;
                        }
                        html = safe_append(html, &html_len, &html_cap, ri, strlen(ri));
                        if (!html)
                        {
                            free(ri);
                            return NULL;
                        }
                        snprintf(tag, sizeof(tag), "</h%zu>\n", level);
                        html = safe_append(html, &html_len, &html_cap, tag, strlen(tag));
                        free(ri);
                        if (!html)
                            return NULL;
                    }
                    else
                    {
                        goto handle_paragraph_block_final_v3;
                    }
                }
                // Horizontal Rule...
                else if (c_len >= 3 && (strncmp(c_start, "---", 3) == 0 || strncmp(c_start, "***", 3) == 0 || strncmp(c_start, "___", 3) == 0))
                {
                    bool only_hr = true;
                    for (size_t i = 0; i < c_len; ++i)
                        if (!strchr("-* _\t", c_start[i]))
                            only_hr = false;
                    if (only_hr)
                    {
                        LOG_DEBUG("Detected HR\n");
                        if (in_p)
                        {
                            html = safe_append(html, &html_len, &html_cap, "</p>\n", 5);
                            in_p = false;
                        }
                        if (!html)
                            return NULL;
                        html = safe_append(html, &html_len, &html_cap, "<hr>\n", 5);
                        if (!html)
                            return NULL;
                    }
                    else
                    {
                        goto handle_paragraph_block_final_v3;
                    }
                }
                // Paragraph...
                else
                {
                handle_paragraph_block_final_v3:
                    LOG_DEBUG("Handling as paragraph line\n");
                    if (!in_p)
                    {
                        html = safe_append(html, &html_len, &html_cap, "<p>", 3);
                        in_p = true;
                        LOG_DEBUG("Opened paragraph\n");
                    }
                    else
                    {
                        html = safe_append(html, &html_len, &html_cap, " ", 1);
                    }
                    if (!html)
                        return NULL;
                    // Use c_start and c_len directly here
                    char *lt = (char *)malloc(c_len + 1);
                    if (!lt)
                    {
                        free(html);
                        return NULL;
                    }
                    strncpy(lt, c_start, c_len);
                    lt[c_len] = '\0';
                    char *ri = render_inline_markdown(lt);
                    free(lt);
                    if (!ri)
                    {
                        free(html);
                        return NULL;
                    }
                    html = safe_append(html, &html_len, &html_cap, ri, strlen(ri));
                    free(ri);
                    if (!html)
                        return NULL;
                }
            } // End block element processing (if not ``` start fence)
        } // End if/else !in_pre

        p = line_end + (line_end < end); // Move to next line start
    } // End while loop

    // Clean up dangling tags at the end
    if (in_p)
    {
        html = safe_append(html, &html_len, &html_cap, "</p>\n", 5);
    }
    if (!html)
        return NULL;
    LOG_DEBUG("Closed dangling paragraph\n");
    if (in_ul)
    {
        html = safe_append(html, &html_len, &html_cap, "</ul>\n", 6);
    }
    if (!html)
        return NULL;
    LOG_DEBUG("Closed dangling UL\n");
    if (in_ol)
    {
        html = safe_append(html, &html_len, &html_cap, "</ol>\n", 6);
    }
    if (!html)
        return NULL;
    LOG_DEBUG("Closed dangling OL\n");
    // Close pre block if EOF is reached while still inside (shouldn't happen with valid MD)
    if (in_pre)
    {
        html = safe_append(html, &html_len, &html_cap, "</code></pre>\n", 14);
    }
    if (!html)
        return NULL;
    LOG_DEBUG("Closed dangling PRE block at EOF\n");

    LOG_INFO("Finished markdown render, final HTML length %zu\n", html_len);
    return html;
}

// --- Utility: Print Usage ---
/**
 * @brief Prints the program usage information to stderr
 * 
 * Displays the supported command-line arguments and their descriptions,
 * including render mode, base directory, IP, port, verbosity, and help options.
 * 
 * @param prog_name The name of the program executable
 */
void print_usage(const char *prog_name)
{
    fprintf(stderr, "Usage: %s [options]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --render <mode>   Rendering mode: 'frontend' (default) or 'backend'.\n");
    fprintf(stderr, "  --dir <path>      Base directory to serve files from (default: '.').\n");
    fprintf(stderr, "  --ip <address>    IP address to listen on (default: '%s').\n", DEFAULT_IP);
    fprintf(stderr, "  --port <number>   Port number to listen on (default: %d).\n", DEFAULT_PORT);
    fprintf(stderr, "  -v, -vv, -vvv     Increase verbosity level (1, 2, or 3).\n");
    fprintf(stderr, "  -h, --help        Show this help message.\n");
}

// --- Utility: Path Safety Check ---
/**
 * @brief Validates that a relative path is safe to access
 * 
 * Performs security checks on a path to prevent directory traversal attacks
 * and other unsafe access patterns:
 * - Rejects paths containing ".." (parent directory references)
 * - Rejects absolute paths (starting with / or \)
 * - On Windows, rejects paths with drive letters (e.g., C:)
 * - On Windows, rejects reserved device names (CON, PRN, etc.)
 * - Restricts allowed characters to alphanumeric plus /\_-~.
 * 
 * @param relative_path The path to validate
 * @return bool true if the path is safe to access, false otherwise
 */
bool is_path_safe(const char *relative_path)
{
    if (!relative_path)
        return false;
    if (strstr(relative_path, ".."))
        return false;
    if (relative_path[0] == PATH_SEPARATOR || relative_path[0] == '/')
        return false;
#ifdef _WIN32
    if (strlen(relative_path) >= 2 && isalpha((unsigned char)relative_path[0]) && relative_path[1] == ':')
        return false;
    const char *fn = strrchr(relative_path, PATH_SEPARATOR);
    fn = fn ? fn + 1 : relative_path;
    char ntc[MAX_PATH_LEN];
    strncpy(ntc, fn, sizeof(ntc) - 1);
    ntc[sizeof(ntc) - 1] = '\0';
    char *d = strrchr(ntc, '.');
    if (d)
        *d = '\0';
    const char *rsv[] = {"CON", "PRN", "AUX", "NUL", "COM1", "LPT1", /*etc*/};
    for (size_t i = 0; i < sizeof(rsv) / sizeof(rsv[0]); ++i)
        if (strcasecmp(ntc, rsv[i]) == 0)
            return false;
#endif
    for (const char *p = relative_path; *p; ++p)
        if (!isalnum((unsigned char)*p) && !strchr("/\\._-~", *p))
            return false;
    return true;
}

// --- Utility: Ensure Directory Exists ---
/**
 * @brief Ensures all directories in a given file path exist, creating them if needed
 * 
 * Given a file path (e.g., "dir/subdir/file.txt"), this function:
 * 1. Extracts the directory portion ("dir/subdir")
 * 2. Checks if it exists
 * 3. If not, recursively ensures parent directories exist
 * 4. Creates the directory if needed
 * 
 * This is useful when serving or creating files in potentially
 * non-existent subdirectories.
 * 
 * @param filepath The file path whose directory structure should exist
 * @return bool true if directories exist or were created, false on error
 */
bool ensure_directory_exists(const char *filepath)
{
    char *path_copy = strdup(filepath);
    if (!path_copy)
    {
        perror("[ERROR] strdup");
        return false;
    }
    bool success = true;
    char *sep = strrchr(path_copy, PATH_SEPARATOR);
    if (sep != NULL && sep != path_copy)
    {
        *sep = '\0';
        LOG_DEBUG("Checking/creating directory: %s\n", path_copy);
        if (access(path_copy, F_OK) != 0)
        {
            if (errno == ENOENT)
            {
                if (!ensure_directory_exists(path_copy))
                    success = false;
                else if (MKDIR(path_copy) != 0 && errno != EEXIST)
                {
                    perror("[ERROR] MKDIR");
                    success = false;
                }
            }
            else
            {
                perror("[ERROR] access");
                success = false;
            }
        }
        else
        {
            struct stat st;
            if (stat(path_copy, &st) == 0)
            {
                if (!S_ISDIR(st.st_mode))
                {
                    LOG_ERROR("Not dir: %s\n", path_copy);
                    success = false;
                }
            }
            else
            {
                perror("[ERROR] stat");
                success = false;
            }
        }
    }
    free(path_copy);
    return success;
}

// --- Utility: File System Checks ---
/**
 * @brief Checks if a file exists and is a regular file (not a directory or special file)
 * 
 * Uses the stat() function to retrieve information about the file and
 * checks if it exists and is a regular file.
 * 
 * @param filename Path to the file to check
 * @return bool true if the file exists and is a regular file, false otherwise
 */
bool file_exists_and_is_regular(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) != 0)
        return false;
    return S_ISREG(st.st_mode);
}

/**
 * @brief Gets the size of a file in bytes
 * 
 * Uses the stat() function to retrieve the size of a regular file.
 * 
 * @param filename Path to the file to check
 * @return long Size of the file in bytes, or -1 on error or if not a regular file
 */
long get_file_size(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return (long)st.st_size;
}

// --- Response Sending Functions (Added Header Logging) ---
/**
 * @brief Sends an HTTP response with the given status, content type, and body
 * 
 * Constructs a proper HTTP response with headers and sends it to the client.
 * Includes Content-Type, Content-Length, Server, and Connection headers.
 * Logs the response headers at detail level for debugging.
 * 
 * @param sock Socket descriptor for the connected client
 * @param status HTTP status line (e.g., "200 OK")
 * @param content_type MIME type of the content (e.g., "text/html")
 * @param body Response body content
 * @param body_len Length of the response body
 */
void send_response(socket_t sock, const char *status, const char *content_type, const char *body, long body_len)
{
    char header_buffer[512];
    int header_len = snprintf(header_buffer, sizeof(header_buffer), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nServer: %s\r\nConnection: close\r\n\r\n", status, content_type, body_len, SERVER_VERSION);
    if (header_len < 0 || (size_t)header_len >= sizeof(header_buffer))
    {
        LOG_ERROR("Header buffer overflow\n");
        return;
    }

    LOG_DETAIL("Sending Response Headers (socket=" SOCKET_FMT "):\n---\n%s---\n", SOCKET_CAST(sock), header_buffer); // Log headers

    ssize_t sent_header = -1;
#ifdef _WIN32
    sent_header = send(sock, header_buffer, header_len, 0);
#else
    sent_header = write(sock, header_buffer, header_len);
#endif
    if ((size_t)sent_header != (size_t)header_len)
    {
        LOG_ERROR("Failed to send full header (%d/%d)\n", (int)sent_header, header_len);
        return;
    }

    if (body != NULL && body_len > 0)
    {
        ssize_t sent_body = -1;
#ifdef _WIN32
        sent_body = send(sock, body, (int)body_len, 0);
#else
        sent_body = write(sock, body, body_len);
#endif
        if (sent_body != body_len)
        {
            LOG_ERROR("Failed to send full body (%d/%ld)\n", (int)sent_body, body_len);
        }
    }
    LOG_DETAIL("Sent response payload (socket=" SOCKET_FMT "): %s (%ld body bytes)\n", SOCKET_CAST(sock), status, body_len);
}

/**
 * @brief Sends the contents of a file as an HTTP response
 * 
 * Opens the specified file, determines its size, sends appropriate HTTP headers,
 * and then streams the file content to the client in chunks. Handles errors
 * during file operations and socket sending, logging issues appropriately.
 * 
 * @param sock Socket descriptor for the connected client
 * @param filename Path to the file to send
 * @param content_type MIME type to use in the Content-Type header
 */
void send_file_response(socket_t sock, const char *filename, const char *content_type)
{
    long file_size = get_file_size(filename);
    if (file_size < 0)
    {
        send_error_response(sock, errno == ENOENT ? 404 : 500, errno == ENOENT ? "NF" : "ISE", "File check failed");
        return;
    }

    char header_buffer[512];
    int header_len = 0;
    if (file_size == 0)
    {
        header_len = snprintf(header_buffer, sizeof(header_buffer), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: 0\r\nServer: %s\r\nConnection: close\r\n\r\n", content_type, SERVER_VERSION);
    }
    else
    {
        header_len = snprintf(header_buffer, sizeof(header_buffer), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nServer: %s\r\nConnection: close\r\n\r\n", content_type, file_size, SERVER_VERSION);
    }

    if (header_len < 0 || (size_t)header_len >= sizeof(header_buffer))
    {
        send_error_response(sock, 500, "ISE", "Header gen error");
        return;
    }

    LOG_DETAIL("Sending File Response Headers (socket=" SOCKET_FMT "):\n---\n%s---\n", SOCKET_CAST(sock), header_buffer); // Log headers

    ssize_t sent_header = -1;
#ifdef _WIN32
    sent_header = send(sock, header_buffer, header_len, 0);
#else
    sent_header = write(sock, header_buffer, header_len);
#endif
    if ((size_t)sent_header != (size_t)header_len)
    {
        LOG_ERROR("Failed to send file header (%d/%d)\n", (int)sent_header, header_len);
        return;
    }

    if (file_size == 0)
    {
        LOG_DETAIL("Sent zero-byte file response for %s\n", filename);
        return;
    } // Done for zero-byte file

    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        send_error_response(sock, 500, "ISE", "File open error");
        return;
    }

    // Send file content in chunks...
    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    long total_sent = 0;
    bool send_error = false;
    while (!send_error && (bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0)
    {
        ssize_t sent_chunk = -1;
        char *cur = file_buffer;
        size_t rem = bytes_read;
        while (!send_error && rem > 0)
        {
#ifdef _WIN32
            sent_chunk = send(sock, cur, (int)rem, 0);
            if (sent_chunk == SOCKET_ERROR)
            {
                send_error = true;
                break;
            }
#else
            sent_chunk = write(sock, cur, rem);
            if (sent_chunk < 0)
            {
                if (errno == EINTR)
                    continue;
                send_error = true;
                break;
            }
#endif
            total_sent += sent_chunk;
            cur += sent_chunk;
            rem -= sent_chunk;
        }
    }
    if (send_error)
    {
        LOG_ERROR("Socket error during file send (%ld/%ld sent)\n", total_sent, file_size);
    }
    else if (ferror(file))
    {
        LOG_ERROR("File read error for %s\n", filename);
    }
    else
    {
        LOG_DETAIL("Sent file %s (%ld bytes)\n", filename, total_sent);
    }
    fclose(file);
}

/**
 * @brief Sends an HTTP error response with the given status code and message
 * 
 * Constructs an HTML error page with the status code, status message, and optional
 * body message. The page includes basic styling and the server version in the footer.
 * Uses send_response() to deliver the error page to the client.
 * 
 * @param sock Socket descriptor for the connected client
 * @param status_code HTTP status code (e.g., 404, 500)
 * @param status_message Short status message (e.g., "Not Found")
 * @param body_message Optional additional message explaining the error
 */
void send_error_response(socket_t sock, int status_code, const char *status_message, const char *body_message)
{
    char body[512];
    const char *safe_msg = body_message ? body_message : "";
    snprintf(body, sizeof(body), "<!DOCTYPE html><html><head><title>%d %s</title></head><body><h1>%d %s</h1><p>%s</p><hr><p><em>%s</em></p></body></html>", status_code, status_message, status_code, status_message, safe_msg, SERVER_VERSION);
    body[sizeof(body) - 1] = '\0';
    char status_line[64];
    snprintf(status_line, sizeof(status_line), "%d %s", status_code, status_message);
    status_line[sizeof(status_line) - 1] = '\0';
    send_response(sock, status_line, "text/html; charset=utf-8", body, strlen(body));
    LOG_INFO("Sent error response: %d %s\n", status_code, status_message);
}