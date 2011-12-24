FreeBSD port of lldb
====================
This repo contains lldb r147097 with additional FreeBSD and Linux patches.
Details on the lldb debugger can be found on the `lldb web site`_.

Checkout and build instructions
===============================

Checkout::

  % svn co -r146622 http://llvm.org/svn/llvm-project/llvm/trunk llvm
  % svn co -r146622 http://llvm.org/svn/llvm-project/cfe/trunk llvm/tools/clang
  % git clone git@github.com:fbsd/lldb.git llvm/tools/lldb

Build::

  % cd llvm
  % ./configure
  % gmake

.. _lldb web site: http://lldb.llvm.org/
