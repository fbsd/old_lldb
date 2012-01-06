"""
Test more expression command sequences with objective-c.
"""

import os, time
import unittest2
import lldb
from lldbtest import *

@unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
class FoundationTestCase2(TestBase):

    mydir = os.path.join("lang", "objc", "foundation")

    def test_more_expr_commands_with_dsym(self):
        """More expression commands for objective-c."""
        self.buildDsym()
        self.more_expr_objc()

    def test_more_expr_commands_with_dwarf(self):
        """More expression commands for objective-c."""
        self.buildDwarf()
        self.more_expr_objc()

    def test_NSArray_expr_commands_with_dsym(self):
        """Test expression commands for NSArray."""
        self.buildDsym()
        self.NSArray_expr()

    def test_NSArray_expr_commands_with_dwarf(self):
        """Test expression commands for NSArray."""
        self.buildDwarf()
        self.NSArray_expr()

    def test_NSString_expr_commands_with_dsym(self):
        """Test expression commands for NSString."""
        self.buildDsym()
        self.NSString_expr()

    def test_NSString_expr_commands_with_dwarf(self):
        """Test expression commands for NSString."""
        self.buildDwarf()
        self.NSString_expr()

    def test_MyString_dump_with_dsym(self):
        """Test dump of a known Objective-C object by dereferencing it."""
        self.buildDsym()
        self.MyString_dump()

    def test_MyString_dump_with_dwarf(self):
        """Test dump of a known Objective-C object by dereferencing it."""
        self.buildDwarf()
        self.MyString_dump()

	def test_NSError_po_with_dsym(self):
		"""Test that po of the result of an unknown method doesn't require a cast."""
		self.buildDsym()
		self.NSError_po()

	def test_NSError_po_with_dwarf(self):
		"""Test that po of the result of an unknown method doesn't require a cast."""
		self.buildDsym()
		self.NSError_po()
		
	def test_NSError_p_with_dsym(self):
		"""Test that p of the result of an unknown method does require a cast."""
		self.buildDsym()
		self.NSError_p()

	def test_NSError_p_with_dwarf(self):
		"""Test that p of the result of an unknown method does require a cast."""
		self.buildDsym()
		self.NSError_p()
				
    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line numbers to break at.
        self.lines = []
        self.lines.append(line_number('main.m', '// Expressions to test here for selector:'))
        self.lines.append(line_number('main.m', '// Expressions to test here for NSArray:'))
        self.lines.append(line_number('main.m', '// Expressions to test here for NSString:'))
        self.lines.append(line_number('main.m', "// Set a breakpoint on '-[MyString description]' and test expressions:"))
        self.lines.append(line_number('main.m', '// Set break point at this line'))
    
    def more_expr_objc(self):
        """More expression commands for objective-c."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Create a bunch of breakpoints.
        for line in self.lines:
            self.expect("breakpoint set -f main.m -l %d" % line, BREAKPOINT_CREATED,
                substrs = ["Breakpoint created:",
                           "file ='main.m', line = %d, locations = 1" % line])

        self.runCmd("run", RUN_SUCCEEDED)

        # Test_Selector:
        self.runCmd("thread backtrace")
        self.expect("expression (char *)sel_getName(sel)",
            substrs = ["(char *)",
                       "length"])

        self.runCmd("process continue")

        # Test_NSArray:
        self.runCmd("thread backtrace")
        self.runCmd("process continue")

        # Test_NSString:
        self.runCmd("thread backtrace")
        self.runCmd("process continue")

        # Test_MyString:
        self.runCmd("thread backtrace")
        self.expect("expression (char *)sel_getName(_cmd)",
            substrs = ["(char *)",
                       "description"])

        self.runCmd("process continue")

    @unittest2.expectedFailure
    # <rdar://problem/8741897> Expressions should support properties
    def NSArray_expr(self):
        """Test expression commands for NSArray."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Break inside Test_NSArray:
        line = self.lines[1]
        self.expect("breakpoint set -f main.m -l %d" % line, BREAKPOINT_CREATED,
            substrs = ["Breakpoint created:",
                       "file ='main.m', line = %d, locations = 1" % line])

        self.runCmd("run", RUN_SUCCEEDED)

        # Test_NSArray:
        self.runCmd("thread backtrace")
        self.expect("expression (int)[nil_mutable_array count]",
            patterns = ["\(int\) \$.* = 0"])
        self.expect("expression (int)[array1 count]",
            patterns = ["\(int\) \$.* = 3"])
        self.expect("expression (int)[array2 count]",
            patterns = ["\(int\) \$.* = 3"])
        self.expect("expression (int)array1.count",
            patterns = ["\(int\) \$.* = 3"])
        self.expect("expression (int)array2.count",
            patterns = ["\(int\) \$.* = 3"])
        self.runCmd("process continue")

    @unittest2.expectedFailure
    # <rdar://problem/8741897> Expressions should support properties
    def NSString_expr(self):
        """Test expression commands for NSString."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Break inside Test_NSString:
        line = self.lines[2]
        self.expect("breakpoint set -f main.m -l %d" % line, BREAKPOINT_CREATED,
            substrs = ["Breakpoint created:",
                       "file ='main.m', line = %d, locations = 1" % line])

        self.runCmd("run", RUN_SUCCEEDED)

        # Test_NSString:
        self.runCmd("thread backtrace")
        self.expect("expression (int)[str length]",
            patterns = ["\(int\) \$.* ="])
        self.expect("expression (int)[str_id length]",
            patterns = ["\(int\) \$.* ="])
        self.expect("expression [str description]",
            patterns = ["\(id\) \$.* = 0x"])
        self.expect("expression [str_id description]",
            patterns = ["\(id\) \$.* = 0x"])
        self.expect("expression str.description")
        self.expect("expression str_id.description")
        self.expect('expression str = @"new"')
        self.expect('expression str = [NSString stringWithFormat: @"%cew", \'N\']')
        self.runCmd("process continue")

    def MyString_dump(self):
        """Test dump of a known Objective-C object by dereferencing it."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)
        
        line = self.lines[4]

        self.expect("breakpoint set -f main.m -l %d" % line, BREAKPOINT_CREATED,
                    substrs = ["Breakpoint created:",
                               "file ='main.m', line = %d, locations = 1" % line])
        
        self.runCmd("run", RUN_SUCCEEDED)
        
        self.expect("expression *my",
            patterns = ["\(MyString\) \$.* = ", "\(MyBase\)", "\(NSObject\)", "\(Class\)"])
        self.runCmd("process continue")

	def NSError_po(self):
		"""Test that po of the result of an unknown method doesn't require a cast."""
		exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)
        
        line = self.lines[4]

        self.expect("breakpoint set -f main.m -l %d" % line, BREAKPOINT_CREATED,
                    substrs = ["Breakpoint created:",
                               "file ='main.m', line = %d, locations = 1" % line])

        self.runCmd("run", RUN_SUCCEEDED)

        self.expect("po [NSError errorWithDomain:@\"Hello\" code:35 userInfo:nil]",
            patterns = ["\(id\) \$.* = ", "Error Domain=Hello", "Code=35", "be completed."])
        self.runCmd("process continue")

	def NSError_p(self):
		"""Test that p of the result of an unknown method does require a cast."""
		exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)
        
        line = self.lines[4]

        self.expect("breakpoint set -f main.m -l %d" % line, BREAKPOINT_CREATED,
                    substrs = ["Breakpoint created:",
                               "file ='main.m', line = %d, locations = 1" % line])

        self.runCmd("run", RUN_SUCCEEDED)

        self.expect("p [NSError errorWithDomain:@\"Hello\" code:35 userInfo:nil]",
                    error = True, 
                    patterns = ["no known method", "cast the message send to the method's return type"])
        self.runCmd("process continue")

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
