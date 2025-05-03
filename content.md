# Hello from the C Server!

This Markdown content was served by a basic C HTTP server.
It is being rendered in your browser using the **marked.js** library.

## Features Demonstrated:

*   Serving static `index.html`.
*   Serving static `content.md`.
*   Client-side rendering using JavaScript (`fetch` and `marked.js`).

```c
// Example code block within Markdown
#include <stdio.h>

int main() {
    printf("Rendered by marked.js!\\n");
    return 0;
}