# fish completion for mdt(1).  Installed under
# $XDG_DATA_HOME/fish/vendor_completions.d by `make install`.
complete -c mdt -f
complete -c mdt -l gfx         -d 'terminal graphics protocol' -x -a 'auto kitty iterm2 sixel none'
complete -c mdt -l math        -d 'maths engine' -x -a 'katex unicode off'
complete -c mdt -l font-px     -d 'override the derived maths font size' -x
complete -c mdt -l theme       -d 'colour theme' -x -a 'dark light nord monochrome'
complete -c mdt -l encoding    -d 'how to read a non-UTF-8 file' -x -a 'auto utf-8 gbk gb18030 big5 cp1252 latin1 off'
complete -c mdt -l lang        -d 'interface language' -x -a 'auto en zh'
complete -c mdt -l entities    -d 'repair an HTML-escaped source' -x -a 'off force'
complete -c mdt -l escapes     -d 'backslash-escaped sources' -x -a 'auto off force'
complete -c mdt -l loose       -d 'markers written without a space' -x -a 'auto off on'
complete -c mdt -l toc         -d 'open the outline panel on start'
complete -c mdt -l panels      -d 'tinted backgrounds for code/quote/table'
complete -c mdt -l terminal-bg -d 'keep the terminal background'
complete -c mdt -l no-scrollbar -d 'hide the scrollbar'
complete -c mdt -l line-numbers -d 'line numbers inside code blocks'
complete -c mdt -l syntax      -d 'code highlighting on'
complete -c mdt -l no-syntax   -d 'code highlighting off'
complete -c mdt -l no-color    -d 'monochrome output'
complete -c mdt -l diag        -d 'print what mdt sees of the file and the terminal'
complete -c mdt -l compat      -d 'no graphics/sync/probes - troubleshooting'
complete -c mdt -l code-fit    -d 'code frames hug the code'
complete -c mdt -l watch       -d 'reload when the file changes on disk'
complete -c mdt -l no-watch    -d 'turn --watch back off'
complete -c mdt -l dump        -d 'print a text rendering' -r
complete -c mdt -l screenshot  -d 'render the screen to a PNG' -r -F
complete -c mdt -l width       -d 'width for --dump/--screenshot' -x
complete -c mdt -l height      -d 'height for --dump/--screenshot' -x
complete -c mdt -l cell        -d 'cell size in pixels' -x
complete -c mdt -l list-caps   -d 'print detected terminal capabilities'
complete -c mdt -s h -l help    -d 'show the help'
complete -c mdt -s v -l version -d 'show the version'
complete -c mdt -a '(__fish_complete_suffix .md)' -d 'Markdown file'
