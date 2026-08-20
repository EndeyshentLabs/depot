if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal indentexpr=GetDepotIndent()
setlocal indentkeys=o,O,!^F,0{,0},=else,=elif,=then

function! GetDepotIndent()
  let lnum = prevnonblank(v:lnum - 1)
  if lnum == 0
    return 0
  endif

  let ind = indent(lnum)
  let prev = getline(lnum)
  let curr = getline(v:lnum)

  if prev =~# '\v\{[ \t]*$' || prev =~# '\v<(if|elif|else)>[ \t]*$'
    let ind += shiftwidth()
  endif

  if curr =~# '\v^\s*\}' || curr =~# '\v^\s*<(else|elif|then)>'
    let ind -= shiftwidth()
  endif

  return ind
endfunction
"
" vim:ts=2 sw=2:
