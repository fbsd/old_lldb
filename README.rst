FreeBSD port of lldb
====================
This repo contains lldb r147646 with additional FreeBSD and Linux patches.
Details on the lldb debugger can be found on the `lldb web site`_.

Checkout and build instructions
===============================

Requirements for FreeBSD:

- FreeBSD 9 or above (posix_spawn and pthread_getthreadid_np are needed)
- Install port lang/python27
- Install port devel/libexecinfo
- Install port devel/swig13

Checkout::

  % svn co -r146622 http://llvm.org/svn/llvm-project/llvm/trunk llvm
  % svn co -r146622 http://llvm.org/svn/llvm-project/cfe/trunk llvm/tools/clang
  % git clone git@github.com:fbsd/lldb.git llvm/tools/lldb

Patch llvm and clang with uncommitted changes needed by lldb::

  % (cd llvm ; sh -c 'for i in tools/lldb/scripts/llvm*.diff; do echo "Patching with file $i"; patch -p0 -i $i; done')
  % (cd llvm/tools/clang ; sh -c 'for i in ../lldb/scripts/clang*.diff; do echo "Patching with file $i"; patch -p0 -i $i; done')

Build::

  % cd llvm
  % ./configure
  % gmake

.. _lldb web site: http://lldb.llvm.org/
