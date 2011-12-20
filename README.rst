FreeBSD port of lldb
====================
This repo contains lldb r146479 with additional FreeBSD patches.

Checkout and build instructions
===============================

% svn co -r145552 http://llvm.org/svn/llvm-project/llvm/trunk llvm
% svn co -r145552 http://llvm.org/svn/llvm-project/cfe/trunk llvm/tools/clang
% git clone git@github.com:fbsd/lldb.git llvm/tools/lldb

% cd llvm
% ./configure
% gmake
