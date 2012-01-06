"""Test the SBData APIs."""

import os
import unittest2
import lldb
import pexpect
from lldbtest import *
from math import fabs

class SBDataAPICase(TestBase):

    mydir = os.path.join("python_api", "sbdata")

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    @python_api_test
    def test_with_dsym_and_run_command(self):
        """Test the SBData APIs."""
        self.buildDsym()
        self.data_api()

    @python_api_test
    def test_with_dwarf_and_run_command(self):
        """Test the SBData APIs."""
        self.buildDwarf()
        self.data_api()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to break on inside main.cpp.
        self.line = line_number('main.cpp', '// set breakpoint here')

    def data_api(self):
        """Test the SBData APIs."""
        self.runCmd("file a.out", CURRENT_EXECUTABLE_SET)
        
        self.expect("breakpoint set -f main.cpp -l %d" % self.line,
                    BREAKPOINT_CREATED,
                    startstr = "Breakpoint created: 1: file ='main.cpp', line = %d, locations = 1" %
                    self.line)
        
        self.runCmd("run", RUN_SUCCEEDED)
        
        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
                    substrs = ['stopped',
                               'stop reason = breakpoint'])
        
        target = self.dbg.GetSelectedTarget()
        
        process = target.GetProcess()
        
        thread = process.GetThreadAtIndex(0)

        frame = thread.GetSelectedFrame()

        foobar = frame.FindVariable('foobar')

        if self.TraceOn():
            print foobar

        data = foobar.GetPointeeData(0, 2)

        if self.TraceOn():
            print data

        offset = 0
        error = lldb.SBError()

        self.assertTrue(data.GetUnsignedInt32(error, offset) == 1, 'foo[0].a == 1')
        offset += 4
        low = data.GetSignedInt16(error, offset)
        offset += 2
        high = data.GetSignedInt16(error, offset)
        offset += 2
        self.assertTrue ((low == 9 and high == 0) or (low == 0 and high == 9), 'foo[0].b == 9')
        self.assertTrue( fabs(data.GetFloat(error, offset) - 3.14) < 1, 'foo[0].c == 3.14')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 8, 'foo[1].a == 8')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 5, 'foo[1].b == 5')
        offset += 4

        self.runCmd("n")

        offset = 16

        self.assertTrue(data.GetUnsignedInt32(error, offset) == 5, 'saved foo[1].b == 5')

        data = foobar.GetPointeeData(1, 1)

        offset = 0

        self.assertTrue(data.GetSignedInt32(error, offset) == 8, 'new foo[1].a == 8')
        offset += 4
        self.assertTrue(data.GetSignedInt32(error, offset) == 7, 'new foo[1].a == 7')
        offset += 8
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 0, 'do not read beyond end')

        star_foobar = foobar.Dereference()
        
        data = star_foobar.GetData()

        if self.TraceOn():
            print data
        
        offset = 0
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 1, 'foo[0].a == 1')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 9, 'foo[0].b == 9')

        foobar_addr = star_foobar.GetLoadAddress()
        foobar_addr += 12

        # http://llvm.org/bugs/show_bug.cgi?id=11579
        # lldb::SBValue::CreateValueFromAddress does not verify SBType::GetPointerType succeeds
        # This should not crash LLDB.
        nothing = foobar.CreateValueFromAddress("nothing", foobar_addr, star_foobar.GetType().GetBasicType(lldb.eBasicTypeInvalid))

        new_foobar = foobar.CreateValueFromAddress("f00", foobar_addr, star_foobar.GetType())

        if self.TraceOn():
            print new_foobar
        
        data = new_foobar.GetData()

        if self.TraceOn():
            print data

        offset = 0
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 8, 'then foo[1].a == 8')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 7, 'then foo[1].b == 7')
        offset += 4
        self.assertTrue(fabs(data.GetFloat(error, offset) - 3.14) < 1, 'foo[1].c == 3.14')

        self.runCmd("n")

        offset = 0
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 8, 'then foo[1].a == 8')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 7, 'then foo[1].b == 7')
        offset += 4
        self.assertTrue(fabs(data.GetFloat(error, offset) - 3.14) < 1, 'foo[1].c == 3.14')

        data = new_foobar.GetData()

        if self.TraceOn():
            print data

        offset = 0
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 8, 'finally foo[1].a == 8')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 7, 'finally foo[1].b == 7')
        offset += 4
        self.assertTrue(fabs(data.GetFloat(error, offset) - 6.28) < 1, 'foo[1].c == 6.28')

        self.runCmd("n")

        barfoo = frame.FindVariable('barfoo')

        data = barfoo.GetData()

        if self.TraceOn():
            print barfoo

        if self.TraceOn():
            print data

        offset = 0
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 1, 'barfoo[0].a = 1')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 2, 'barfoo[0].b == 2')
        offset += 4
        self.assertTrue(fabs(data.GetFloat(error, offset) - 3) < 1, 'barfoo[0].c == 3')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 4, 'barfoo[1].a = 4')
        offset += 4
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 5, 'barfoo[1].b == 5')
        offset += 4
        self.assertTrue(fabs(data.GetFloat(error, offset) - 6) < 1, 'barfoo[1].c == 6')

        new_object = barfoo.CreateValueFromData("new_object",data,barfoo.GetType().GetBasicType(lldb.eBasicTypeInt))

        if self.TraceOn():
            print new_object
        
        self.assertTrue(new_object.GetLoadAddress() == 0xFFFFFFFFFFFFFFFF, 'GetLoadAddress() == invalid')
        self.assertTrue(new_object.AddressOf().IsValid() == False, 'AddressOf() == invalid')
        self.assertTrue(new_object.GetAddress().IsValid() == False, 'GetAddress() == invalid')

        self.assertTrue(new_object.GetValue() == "1", 'new_object == 1')

        data.SetData(error, 'A\0\0\0', data.GetByteOrder(), data.GetAddressByteSize())
        
        data2 = lldb.SBData()
        data2.SetData(error, 'BCD', data.GetByteOrder(), data.GetAddressByteSize())

        data.Append(data2)
        
        if self.TraceOn():
            print data

        # this breaks on EBCDIC
        offset = 0
        self.assertTrue(data.GetUnsignedInt32(error, offset) == 65, 'made-up data == 65')
        offset += 4
        self.assertTrue(data.GetUnsignedInt8(error, offset) == 66, 'made-up data == 66')
        offset += 1
        self.assertTrue(data.GetUnsignedInt8(error, offset) == 67, 'made-up data == 67')
        offset += 1
        self.assertTrue(data.GetUnsignedInt8(error, offset) == 68, 'made-up data == 68')
        offset += 1

        # check the new API calls introduced per LLVM bugzilla enhancement request
        # 11619 (Allow creating SBData values from arrays or primitives in Python)

        data2 = process.GetDataFromCString('hello!')
        self.assertTrue(data2.GetUnsignedInt8(error,0) == 104, 'h == 104')
        self.assertTrue(data2.GetUnsignedInt8(error,1) == 101, 'e == 101')
        self.assertTrue(data2.GetUnsignedInt8(error,2) == 108, 'l == 108')
        self.assertTrue(data2.GetUnsignedInt8(error,3) == 108, 'l == 108')
        self.assertTrue(data2.GetUnsignedInt8(error,4) == 111, 'o == 111')
        self.assertTrue(data2.GetUnsignedInt8(error,5) == 33, '! == 33')
        self.assertTrue(data2.GetUnsignedInt8(error,6) == 0, 'binary 0 terminator')
        
        data2 = process.GetDataFromUnsignedInt64Array([1,2,3,4,5])
        self.assertTrue(data2.GetUnsignedInt64(error,0) == 1, 'data2[0] = 1')
        self.assertTrue(data2.GetUnsignedInt64(error,8) == 2, 'data2[1] = 2')
        self.assertTrue(data2.GetUnsignedInt64(error,16) == 3, 'data2[2] = 3')
        self.assertTrue(data2.GetUnsignedInt64(error,24) == 4, 'data2[3] = 4')
        self.assertTrue(data2.GetUnsignedInt64(error,32) == 5, 'data2[4] = 5')
        
        data2 = process.GetDataFromSignedInt32Array([2, -2])
        self.assertTrue(data2.GetSignedInt32(error,0) == 2, 'signed32 data2[0] = 2')
        self.assertTrue(data2.GetSignedInt32(error,4) == -2, 'signed32 data2[1] = -2')
        
        data2.Append(process.GetDataFromSignedInt64Array([2, -2]))
        self.assertTrue(data2.GetSignedInt32(error,0) == 2, 'signed32 data2[0] = 2')
        self.assertTrue(data2.GetSignedInt32(error,4) == -2, 'signed32 data2[1] = -2')
        self.assertTrue(data2.GetSignedInt64(error,8) == 2, 'signed64 data2[0] = 2')
        self.assertTrue(data2.GetSignedInt64(error,16) == -2, 'signed64 data2[1] = -2')
        
        data2 = process.GetDataFromUnsignedInt32Array([1,2,3,4,5])
        self.assertTrue(data2.GetUnsignedInt32(error,0) == 1, '32-bit data2[0] = 1')
        self.assertTrue(data2.GetUnsignedInt32(error,4) == 2, '32-bit data2[1] = 2')
        self.assertTrue(data2.GetUnsignedInt32(error,8) == 3, '32-bit data2[2] = 3')
        self.assertTrue(data2.GetUnsignedInt32(error,12) == 4, '32-bit data2[3] = 4')
        self.assertTrue(data2.GetUnsignedInt32(error,16) == 5, '32-bit data2[4] = 5')
        
        data2 = process.GetDataFromDoubleArray([3.14,6.28,2.71])
        self.assertTrue( fabs(data2.GetDouble(error,0) - 3.14) < 0.5, 'double data2[0] = 3.14')
        self.assertTrue( fabs(data2.GetDouble(error,8) - 6.28) < 0.5, 'double data2[1] = 6.28')
        self.assertTrue( fabs(data2.GetDouble(error,16) - 2.71) < 0.5, 'double data2[2] = 2.71')
        

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
