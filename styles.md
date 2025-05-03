# Markdown Styles Test Page

This page demonstrates the basic Markdown syntax supported by the server, particularly when using the **backend rendering** mode (`--render backend`).

---

# Heading 1 Example
## Heading 2 Example
### Heading 3 Example
#### Heading 4 Example
##### Heading 5 Example
###### Heading 6 Example

---

This is a paragraph with **bold text** using double asterisks.
This is also __bold text__ using double underscores.

This is *italic text* using single asterisks.
This is also _italic text_ using single underscores.

This paragraph contains `inline code` using single backticks. It should render with a distinct style. Escaped backticks: \`.

Here is [a link to Google](https://google.com).
Here is another [link to a local file](README.md) with escaped brackets \[like this\].

This line has escaped \*italic\* markers and \_underscore\_ markers.

Another paragraph separated by a blank line.

---

## Unordered List

* Item one using asterisk
+ Item two using plus
- Item three using minus
* Item four with _italic_ and `code`.
* Item five with a [link](http://example.com).
    * (Note: Indented sub-lists are not currently supported by the basic backend renderer)

## Ordered List

1. First item
2. Second **bold** item
99. Third item (number doesn't matter for HTML) `code`.

---

Horizontal Rule Test:

---

***

___


## Fenced Code Block Test

```c
#include <stdio.h>

// This is a comment inside a code block
int main(void) {
    printf("Hello from a C code block!\n");
    // Should handle < > & " correctly
    char* test = "<>&\"";
    printf("%s\n", test);
    return 0;
}
```

Another paragraph after the code block.