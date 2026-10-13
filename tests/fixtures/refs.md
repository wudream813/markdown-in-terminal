---
title: A front matter title
author: nobody
---

# Heading with attributes {#custom-id}

Look at [the docs][d], [another][] and the shortcut [d].

Undefined [nope] and [text][missing] stay as text.

```sh
# a definition inside a fence is just code
[label]: https://not-a-definition.example
```

After the code block [d] still resolves, and so does [a label with spaces].

[d]: https://example.com/docs
[another]: <https://example.com/another>
[a label with spaces]: https://example.com/spaced "with a title"

The end.
