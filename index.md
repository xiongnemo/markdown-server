# Welcome to the C Markdown Server Demo!

This content is being served by the `markdown_server` C program. Depending on the server's configuration (`--render` option), it might be:

1.  **Frontend Rendered:** Rendered in your browser using JavaScript ([marked.js](https://github.com/markedjs/marked)). The server sends raw Markdown for `/path.md` requests.
2.  **Backend Rendered:** Rendered directly to HTML by the C server *before* being sent to your browser. The server-side renderer supports basic Markdown features including headings, paragraphs, bold, italic, lists, horizontal rules, `code spans`, fenced code blocks, and links.

The C server delivers a basic HTML shell (embedded within the executable in frontend mode, or generated with content in backend mode), which includes a 'Home' link at the top-left.

## Before you start

*   Make sure to read the [README](README) file to understand how to build, configure, and run the C server.
*   Check out the Markdown [Styles Test](styles) page to see examples of supported syntax (especially in backend rendering mode).

## Available Pages:

Here are some example pages you can create and visit (assuming the server is running in the same directory):

*   [Home Page](/): You are here! (Requires `index.md` to exist).
*   [About Page](/about): Create an `about.md` file.
*   [Styles Test](/styles): View `styles.md`.
*   [First Blog Post](/posts/first-post): Create `posts/first-post.md` (create the `posts` directory first).
*   [Another Blog Post](/posts/another): Create `posts/another.md`.

*(Remember to create the corresponding `.md` files and directories relative to the server's base directory for these links to work!)*

## Testing

You can also try accessing a page that doesn't map to an existing `.md` file:

*   [Non-Existent Page](/nonexistent)

---

*Powered by a C HTTP Server (`markdown_server`).*