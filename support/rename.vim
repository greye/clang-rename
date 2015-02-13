function! ClangRename(name)
    let offset = line2byte(line(".")) + col(".") - 2
    call system("clang-rename -i -offset=" . offset . " -new-name=" . a:name . " " . expand("%:p"))
    edit
endfunction
