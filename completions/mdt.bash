# bash completion for mdt(1).  Installed by `make install`, or source it:
#   source completions/mdt.bash
_mdt() {
  local cur prev opts
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
  opts="--gfx --math --font-px --theme --encoding --entities --escapes --loose --lang
        --toc --panels --terminal-bg --no-scrollbar --line-numbers --syntax
        --no-syntax --no-color --diag --compat --code-fit --watch --no-watch
        --dump --screenshot
        --width --height --cell --list-caps --help --version -h -v"

  case "$prev" in
    --gfx)      COMPREPLY=($(compgen -W "auto kitty iterm2 sixel none" -- "$cur")); return 0 ;;
    --math)     COMPREPLY=($(compgen -W "katex unicode off" -- "$cur")); return 0 ;;
    --theme)    COMPREPLY=($(compgen -W "dark light nord monochrome" -- "$cur")); return 0 ;;
    --encoding) COMPREPLY=($(compgen -W "auto utf-8 gbk gb18030 big5 cp1252 latin1 off" -- "$cur")); return 0 ;;
    --lang) COMPREPLY=($(compgen -W "auto en zh" -- "$cur")); return 0 ;;
    --entities) COMPREPLY=($(compgen -W "off force" -- "$cur")); return 0 ;;
    --escapes)  COMPREPLY=($(compgen -W "auto off force" -- "$cur")); return 0 ;;
    --loose)    COMPREPLY=($(compgen -W "auto off on" -- "$cur")); return 0 ;;
    --width|--height|--font-px|--cell) return 0 ;;
    --screenshot|--dump) COMPREPLY=($(compgen -f -- "$cur")); return 0 ;;
  esac

  case "$cur" in
    --gfx=*)      COMPREPLY=($(compgen -W "auto kitty iterm2 sixel none" -- "${cur#*=}" | sed 's/^/--gfx=/')); return 0 ;;
    --math=*)     COMPREPLY=($(compgen -W "katex unicode off" -- "${cur#*=}" | sed 's/^/--math=/')); return 0 ;;
    --theme=*)    COMPREPLY=($(compgen -W "dark light nord monochrome" -- "${cur#*=}" | sed 's/^/--theme=/')); return 0 ;;
    --encoding=*) COMPREPLY=($(compgen -W "auto utf-8 gbk gb18030 big5 cp1252 latin1 off" -- "${cur#*=}" | sed 's/^/--encoding=/')); return 0 ;;
    --lang=*) COMPREPLY=($(compgen -W "auto en zh" -- "${cur#*=}" | sed 's/^/--lang=/')); return 0 ;;
    --*)          COMPREPLY=($(compgen -W "$opts" -- "$cur")); return 0 ;;
  esac

  # files: .md first, then anything else
  COMPREPLY=($(compgen -f -X '!*.md' -- "$cur"))
  [ ${#COMPREPLY[@]} -eq 0 ] && COMPREPLY=($(compgen -f -- "$cur"))
  # with a file already given, only options make sense
  local i
  for ((i = 1; i < COMP_CWORD; i++)); do
    case "${COMP_WORDS[i]}" in
      -*) ;;
      *) COMPREPLY=($(compgen -W "$opts" -- "$cur")); break ;;
    esac
  done
  return 0
}
complete -F _mdt mdt
