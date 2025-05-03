# Markdown Server (`markdown_server`)

A single-file, single-threaded HTTP server written in C (using POSIX/Winsock sockets) designed to serve Markdown files (`.md`) from the local filesystem. It supports two rendering modes configurable via command-line arguments.

## Features

* Basic HTTP/1.1 GET request handling.
* **Dual Rendering Modes:** Supports both client-side (default, using `marked.js`) and server-side (basic internal C renderer) rendering.
* **Command-Line Configuration:**
  * `--render <mode>`: Choose `frontend` or `backend` rendering.
  * `--dir <path>`: Specify the base directory to serve files from.
  * `--ip <address>`: Set the IP address to bind to.
  * `--port <number>`: Set the port to listen on.
  * `-v`, `-vv`, `-vvv`: Increase logging verbosity.
  * `-h`, `--help`: Display usage information.
* Serves an **embedded** HTML shell in frontend mode.
* Top-left "Home" link on all pages.
* Dynamically serves/renders `.md` files based on the requested path relative to the server's base directory.
* Maps `/` to `index.md` within the base directory.
* Handles missing `index.md` gracefully in both modes by serving a default message.
* Returns 404 Not Found errors for missing `.md` files.
* Basic **Server-Side Markdown Renderer** supporting: Headings (`#`-`######`), paragraphs, bold (`**`/`__`), italic (`*`/`_`), single backtick code spans (`` `code` ``), links (`[text](url)`), horizontal rules (`---`/`***`/`___`), simple unordered/ordered lists, and fenced code blocks (`` ``` ``).
* Basic path safety checks implemented.
* Attempts cross-platform compatibility (POSIX sockets for Linux/macOS, Winsock2 for Windows via MinGW/GCC).
* Self-contained server logic (including embedded HTML) within a single C source file.

## Dependencies

**Build-time:**

* A C compiler supporting C2x or later (GCC or Clang recommended).
* (Windows) A MinGW/GCC build environment.
* (Windows) Winsock library (`ws2_32`) - linked via the `-lws2_32` flag.

**Runtime:**

* The compiled executable.
* Your Markdown content files (`.md`) placed relative to the specified base directory (`--dir`).
* (Frontend Mode Only) Internet connection for the client browser to fetch `marked.js` from CDN.

## Build Instructions

Navigate to the directory containing `markdown_server.c`.

**Linux / macOS:**

```bash
gcc markdown_server.c -o markdown_server -Wall -Wextra -std=c2x
```

**Windows (using MinGW/GCC):**

```bash
gcc markdown_server.c -o markdown_server.exe -lws2_32 -Wall -Wextra -std=c2x
```

It should be compiled with no warnings or errors. If you get any, please report.

## Directory Structure Example

When running the server, your directory might look like this:

```
.
├── markdown_server (or markdown_server.exe)  # Compiled executable
├── markdown_server.c                         # C source code
├── README.md                                 # This file
├── index.md                                  # Content for "/"
├── styles.md                                 # Markdown styles test page
├── about.md                                  # Example content for "/about"
└── posts/                                    # Example subdirectory
    ├── first-post.md                         # Example content for "/posts/first-post"
    └── another.md                            # Example content for "/posts/another"
```

## Usage

1. Compile the server.
2. Place the executable and your `.md` files in the desired directory structure.
3. Run the executable from your terminal, optionally using command-line flags:

   * **Default (Frontend Rendering):**

     ```bash
     # Linux/macOS:
     ./markdown_server
     # Windows:
     .\markdown_server.exe
     ```
   * **Backend Rendering:**

     ```bash
     ./markdown_server --render backend
     ```
   * **Custom Directory & Port (Backend):**

     ```bash
     ./markdown_server --render backend --dir /path/to/my/docs --port 9000
     ```
   * **Verbose Logging (Detail Level):**

     ```bash
     ./markdown_server -vv
     ```
4. The terminal will show server configuration and the listening address (e.g., `http://0.0.0.0:8080`).
5. Open your web browser and navigate to the displayed address (using `localhost` or your machine's IP if needed). If you want, you can also use `curl` to test the server:

   ```bash
   curl http://localhost:8080
   ```
6. Navigate to other pages by changing the URL path (e.g., `/styles`, `/about`).
7. Press `Ctrl+C` in the terminal to stop the server.

## About Rendering Modes

1. **Frontend Rendering (`--render frontend`, Default):**

   * For navigation requests (e.g., `/`, `/about`), the server sends a minimal embedded HTML shell.
   * This HTML shell contains JavaScript that fetches [marked.js](https://github.com/markedjs/marked) from a CDN.
   * The JavaScript determines the required Markdown file (e.g., `/` -> `/index.md`, `/about` -> `/about.md`) based on the URL.
   * It `fetch`es the raw content of the corresponding `.md` file from the C server.
   * The server sends the raw Markdown text (e.g., content of `./index.md`, `./about.md`).
   * The client-side JavaScript uses `marked.js` to parse the Markdown into HTML and display it.
2. **Backend Rendering (`--render backend`):**

   * For any request (e.g., `/`, `/about`), the server determines the corresponding `.md` file (e.g., `./index.md`, `./about.md`).
   * It reads the Markdown file content.
   * It uses its **internal basic C Markdown renderer** to convert the Markdown text directly into an HTML fragment.
   * This HTML fragment is embedded within a basic HTML template (`ssr_html_template` within the code).
   * The complete HTML page is sent directly to the browser. No client-side JavaScript rendering is involved (or needed).

## Limitations

* **Demo:** NOT for PRODUCTION use. It's just a quick demo, serves as a proof of concept & some specific usages.
* **Single-Threaded:** Handles one connection at a time.
* **Basic HTTP Parsing:** Only simple GET requests are understood.
* **Basic Server-Side Renderer:** The internal C Markdown renderer is *very* basic and not fully compliant with standards like CommonMark or GFM. It lacks features like nested lists, blockquotes, complex inline nesting, tables, image syntax, etc. Frontend rendering relies on the more complete `marked.js`.
* **Security:** Path safety checks are basic. Do not expose to untrusted networks/users. No HTTPS. Not hardened for production use.
* **Error Handling:** Basic error checking.
* **Performance:** Synchronous I/O, not optimized. SSR reads the whole file into memory.
* **CDN Dependency:** Frontend mode relies on an external CDN for `marked.js`.

## License

```
MIT License

Copyright (c) 2025 Nemo Xiong

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
