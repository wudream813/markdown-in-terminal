# Footnotes in mdt

GitHub-style footnotes[^one] are numbered where they are first used[^two],
and a label that is not defined[^missing] stays as written.  Repeating[^one]
does not renumber it.

Apple
: a definition list item that also cites a source[^one].

```
[^one]: not a definition, just code
```

[^unused]: never referenced

[^one]: The first note, with the [project page](https://example.com/footnotes).
[^two]: The second one, whose text wraps over
  two source lines.
