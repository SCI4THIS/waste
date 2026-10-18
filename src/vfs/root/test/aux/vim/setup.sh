# Input preparation only; installed.wast owns the result assertions.
/usr/bin/vim --version > /tmp/vim-package-version.txt
/usr/bin/vim -u NONE -i NONE -n -es /tmp/vim-package.txt <<'VIM_INPUT'
a
VIM_PACKAGE_SAVED
.
wq
VIM_INPUT
