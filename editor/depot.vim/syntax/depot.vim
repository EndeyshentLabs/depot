if exists("b:current_syntax")
  finish
endif

syn match   depotNumber  "\<\d\+"
syn match   depotNumber  "\<\d\+#[a-zA-Z0-9]\+"
syn keyword depotBoolean true false

syn match  depotEscape /\\[nrtvab\\0]/ contained
syn region depotString start=/"/ end=/"/ skip=/\\"/ contains=depotEscape

syn match   depotOperator #\<[-+*/;{}=]\>#
syn match   depotOperator #\<[<>]=\?\>#
syn match   depotOperator #\<!=>#
syn match   depotOperator #\<--\>#
syn match   depotOperator #\<[@!]\(\(64\)\|\(32\)\|\(16\)\|\(8\)\)\>#
syn keyword depotOperator drop dup swap rot over mod >int64 >ptr >bool

syn keyword depotConditional if elif else then
syn keyword depotRepeat      while

syn keyword depotKeywords link include nextgroup=depotString
syn keyword depotKeywords proc extern memory

syn keyword depotTypes int64 ptr bool

syn keyword depotTodo    TODO FIXME XXX HACK NOTE contained
syn region  depotComment start="//" end="$" contains=depotTodo,@Spell

hi def link depotNumber      Number
hi def link depotBoolean     Boolean
hi def link depotEscape      Special
hi def link depotString      String
hi def link depotOperator    Operator
hi def link depotConditional Conditional
hi def link depotRepeat      Repeat
hi def link depotKeywords    Keyword
hi def link depotTypes       Type
hi def link depotComment     Comment
hi def link depotTodo        Todo

let b:current_syntax = "depot"

" vim:ts=2 sw=2:
