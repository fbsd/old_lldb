//===-- CommandObjectExpression.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectExpression.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Interpreter/Args.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/InputReader.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Expression/ClangExpressionVariable.h"
#include "lldb/Expression/ClangUserExpression.h"
#include "lldb/Expression/ClangFunction.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Host/Host.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "llvm/ADT/StringRef.h"

using namespace lldb;
using namespace lldb_private;

CommandObjectExpression::CommandOptions::CommandOptions () :
    OptionGroup()
{
}


CommandObjectExpression::CommandOptions::~CommandOptions ()
{
}

OptionDefinition
CommandObjectExpression::CommandOptions::g_option_table[] =
{
    { LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "dynamic-value",      'd', required_argument, NULL, 0, eArgTypeBoolean,    "Upcast the value resulting from the expression to its dynamic type if available."},
    { LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "unwind-on-error",    'u', required_argument, NULL, 0, eArgTypeBoolean,    "Clean up program state if the expression causes a crash, breakpoint hit or signal."},
    { LLDB_OPT_SET_2                 , false, "object-description", 'o', no_argument,       NULL, 0, eArgTypeNone,       "Print the object description of the value resulting from the expression."},
};


uint32_t
CommandObjectExpression::CommandOptions::GetNumDefinitions ()
{
    return sizeof(g_option_table)/sizeof(OptionDefinition);
}

Error
CommandObjectExpression::CommandOptions::SetOptionValue (CommandInterpreter &interpreter,
                                                         uint32_t option_idx,
                                                         const char *option_arg)
{
    Error error;

    const char short_option = (char) g_option_table[option_idx].short_option;

    switch (short_option)
    {
      //case 'l':
      //if (language.SetLanguageFromCString (option_arg) == false)
      //{
      //    error.SetErrorStringWithFormat("invalid language option argument '%s'", option_arg);
      //}
      //break;

    case 'o':
        print_object = true;
        break;
        
    case 'd':
        {
            bool success;
            bool result;
            result = Args::StringToBoolean(option_arg, true, &success);
            if (!success)
                error.SetErrorStringWithFormat("invalid dynamic value setting: \"%s\"", option_arg);
            else
            {
                if (result)
                    use_dynamic = eLazyBoolYes;  
                else
                    use_dynamic = eLazyBoolNo;
            }
        }
        break;
        
    case 'u':
        {
            bool success;
            unwind_on_error = Args::StringToBoolean(option_arg, true, &success);
            if (!success)
                error.SetErrorStringWithFormat("could not convert \"%s\" to a boolean value.", option_arg);
            break;
        }
    default:
        error.SetErrorStringWithFormat("invalid short option character '%c'", short_option);
        break;
    }

    return error;
}

void
CommandObjectExpression::CommandOptions::OptionParsingStarting (CommandInterpreter &interpreter)
{
    use_dynamic = eLazyBoolCalculate;
    print_object = false;
    unwind_on_error = true;
    show_types = true;
    show_summary = true;
}

const OptionDefinition*
CommandObjectExpression::CommandOptions::GetDefinitions ()
{
    return g_option_table;
}

CommandObjectExpression::CommandObjectExpression (CommandInterpreter &interpreter) :
    CommandObject (interpreter,
                   "expression",
                   "Evaluate a C/ObjC/C++ expression in the current program context, using variables currently in scope.",
                   NULL),
    m_option_group (interpreter),
    m_format_options (eFormatDefault),
    m_command_options (),
    m_expr_line_count (0),
    m_expr_lines ()
{
  SetHelpLong(
"Examples: \n\
\n\
   expr my_struct->a = my_array[3] \n\
   expr -f bin -- (index * 8) + 5 \n\
   expr char c[] = \"foo\"; c[0]\n");

    CommandArgumentEntry arg;
    CommandArgumentData expression_arg;

    // Define the first (and only) variant of this arg.
    expression_arg.arg_type = eArgTypeExpression;
    expression_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the argument entry.
    arg.push_back (expression_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back (arg);
    
    // Add the "--format" and "--gdb-format"
    m_option_group.Append (&m_format_options, OptionGroupFormat::OPTION_GROUP_FORMAT | OptionGroupFormat::OPTION_GROUP_GDB_FMT, LLDB_OPT_SET_1);
    m_option_group.Append (&m_command_options);
    m_option_group.Finalize();
}

CommandObjectExpression::~CommandObjectExpression ()
{
}

Options *
CommandObjectExpression::GetOptions ()
{
    return &m_option_group;
}


bool
CommandObjectExpression::Execute
(
    Args& command,
    CommandReturnObject &result
)
{
    return false;
}


size_t
CommandObjectExpression::MultiLineExpressionCallback
(
    void *baton, 
    InputReader &reader, 
    lldb::InputReaderAction notification,
    const char *bytes, 
    size_t bytes_len
)
{
    CommandObjectExpression *cmd_object_expr = (CommandObjectExpression *) baton;
    bool batch_mode = reader.GetDebugger().GetCommandInterpreter().GetBatchCommandMode();
    
    switch (notification)
    {
    case eInputReaderActivate:
        if (!batch_mode)
        {
            StreamSP async_strm_sp(reader.GetDebugger().GetAsyncOutputStream());
            if (async_strm_sp)
            {
                async_strm_sp->PutCString("Enter expressions, then terminate with an empty line to evaluate:\n");
                async_strm_sp->Flush();
            }
        }
        // Fall through
    case eInputReaderReactivate:
        break;

    case eInputReaderDeactivate:
        break;

    case eInputReaderAsynchronousOutputWritten:
        break;
        
    case eInputReaderGotToken:
        ++cmd_object_expr->m_expr_line_count;
        if (bytes && bytes_len)
        {
            cmd_object_expr->m_expr_lines.append (bytes, bytes_len + 1);
        }

        if (bytes_len == 0)
            reader.SetIsDone(true);
        break;
        
    case eInputReaderInterrupt:
        cmd_object_expr->m_expr_lines.clear();
        reader.SetIsDone (true);
        if (!batch_mode)
        {
            StreamSP async_strm_sp (reader.GetDebugger().GetAsyncOutputStream());
            if (async_strm_sp)
            {
                async_strm_sp->PutCString("Expression evaluation cancelled.\n");
                async_strm_sp->Flush();
            }
        }
        break;
        
    case eInputReaderEndOfFile:
        reader.SetIsDone (true);
        break;
        
    case eInputReaderDone:
		if (cmd_object_expr->m_expr_lines.size() > 0)
        {
            StreamSP output_stream = reader.GetDebugger().GetAsyncOutputStream();
            StreamSP error_stream = reader.GetDebugger().GetAsyncErrorStream();
            cmd_object_expr->EvaluateExpression (cmd_object_expr->m_expr_lines.c_str(), 
                                                 output_stream.get(), 
                                                 error_stream.get());
            output_stream->Flush();
            error_stream->Flush();
        }
        break;
    }

    return bytes_len;
}

bool
CommandObjectExpression::EvaluateExpression 
(
    const char *expr, 
    Stream *output_stream, 
    Stream *error_stream,
    CommandReturnObject *result
)
{
    Target *target = m_exe_ctx.GetTargetPtr();
    
    if (!target)
        target = Host::GetDummyTarget(m_interpreter.GetDebugger()).get();
    
    if (target)
    {
        lldb::ValueObjectSP result_valobj_sp;

        ExecutionResults exe_results;
        
        bool keep_in_memory = true;
        lldb::DynamicValueType use_dynamic;
        // If use dynamic is not set, get it from the target:
        switch (m_command_options.use_dynamic)
        {
        case eLazyBoolCalculate:
            use_dynamic = target->GetPreferDynamicValue();
            break;
        case eLazyBoolYes:
            use_dynamic = lldb::eDynamicCanRunTarget;
            break;
        case eLazyBoolNo:
            use_dynamic = lldb::eNoDynamicValues;
            break;
        }
        
        exe_results = target->EvaluateExpression (expr, 
                                                  m_exe_ctx.GetFramePtr(),
                                                  eExecutionPolicyOnlyWhenNeeded,
                                                  m_command_options.print_object,
                                                  m_command_options.unwind_on_error,
                                                  keep_in_memory, 
                                                  use_dynamic, 
                                                  result_valobj_sp);
        
        if (exe_results == eExecutionInterrupted && !m_command_options.unwind_on_error)
        {
            uint32_t start_frame = 0;
            uint32_t num_frames = 1;
            uint32_t num_frames_with_source = 0;
            Thread *thread = m_exe_ctx.GetThreadPtr();
            if (thread)
            {
                thread->GetStatus (result->GetOutputStream(), 
                                   start_frame, 
                                   num_frames, 
                                   num_frames_with_source);
            }
            else 
            {
                Process *process = m_exe_ctx.GetProcessPtr();
                if (process)
                {
                    bool only_threads_with_stop_reason = true;
                    process->GetThreadStatus (result->GetOutputStream(), 
                                              only_threads_with_stop_reason, 
                                              start_frame, 
                                              num_frames, 
                                              num_frames_with_source);
                }
            }
        }

        if (result_valobj_sp)
        {
            if (result_valobj_sp->GetError().Success())
            {
                Format format = m_format_options.GetFormat();
                if (format != eFormatDefault)
                    result_valobj_sp->SetFormat (format);

                ValueObject::DumpValueObject (*(output_stream),
                                              result_valobj_sp.get(),   // Variable object to dump
                                              result_valobj_sp->GetName().GetCString(),// Root object name
                                              0,                        // Pointer depth to traverse (zero means stop at pointers)
                                              0,                        // Current depth, this is the top most, so zero...
                                              UINT32_MAX,               // Max depth to go when dumping concrete types, dump everything...
                                              m_command_options.show_types,     // Show types when dumping?
                                              false,                    // Show locations of variables, no since this is a host address which we don't care to see
                                              m_command_options.print_object,   // Print the objective C object?
                                              use_dynamic,
                                              true,                     // Use synthetic children if available
                                              true,                     // Scope is already checked. Const results are always in scope.
                                              false,                    // Don't flatten output
                                              0,                        // Always use summaries (you might want an option --no-summary like there is for frame variable)
                                              false);                   // Do not show more children than settings allow
                if (result)
                    result->SetStatus (eReturnStatusSuccessFinishResult);
            }
            else
            {
                if (result_valobj_sp->GetError().GetError() == ClangUserExpression::kNoResult)
                {
                    error_stream->PutCString("<no result>\n");
                    
                    if (result)
                        result->SetStatus (eReturnStatusSuccessFinishResult);
                }
                else
                {
                    const char *error_cstr = result_valobj_sp->GetError().AsCString();
                    if (error_cstr && error_cstr[0])
                    {
                        int error_cstr_len = strlen (error_cstr);
                        const bool ends_with_newline = error_cstr[error_cstr_len - 1] == '\n';
                        if (strstr(error_cstr, "error:") != error_cstr)
                            error_stream->PutCString ("error: ");
                        error_stream->Write(error_cstr, error_cstr_len);
                        if (!ends_with_newline)
                            error_stream->EOL();
                    }
                    else
                    {
                        error_stream->PutCString ("error: unknown error\n");
                    }
                    
                    if (result)
                        result->SetStatus (eReturnStatusFailed);
                }
            }
        }
    }
    else
    {
        error_stream->Printf ("error: invalid execution context for expression\n");
        return false;
    }
        
    return true;
}

bool
CommandObjectExpression::ExecuteRawCommandString
(
    const char *command,
    CommandReturnObject &result
)
{
    m_exe_ctx = m_interpreter.GetExecutionContext();

    m_option_group.NotifyOptionParsingStarting();

    const char * expr = NULL;

    if (command[0] == '\0')
    {
        m_expr_lines.clear();
        m_expr_line_count = 0;
        
        InputReaderSP reader_sp (new InputReader(m_interpreter.GetDebugger()));
        if (reader_sp)
        {
            Error err (reader_sp->Initialize (CommandObjectExpression::MultiLineExpressionCallback,
                                              this,                         // baton
                                              eInputReaderGranularityLine,  // token size, to pass to callback function
                                              NULL,                         // end token
                                              NULL,                         // prompt
                                              true));                       // echo input
            if (err.Success())
            {
                m_interpreter.GetDebugger().PushInputReader (reader_sp);
                result.SetStatus (eReturnStatusSuccessFinishNoResult);
            }
            else
            {
                result.AppendError (err.AsCString());
                result.SetStatus (eReturnStatusFailed);
            }
        }
        else
        {
            result.AppendError("out of memory");
            result.SetStatus (eReturnStatusFailed);
        }
        return result.Succeeded();
    }

    if (command[0] == '-')
    {
        // We have some options and these options MUST end with --.
        const char *end_options = NULL;
        const char *s = command;
        while (s && s[0])
        {
            end_options = ::strstr (s, "--");
            if (end_options)
            {
                end_options += 2; // Get past the "--"
                if (::isspace (end_options[0]))
                {
                    expr = end_options;
                    while (::isspace (*expr))
                        ++expr;
                    break;
                }
            }
            s = end_options;
        }

        if (end_options)
        {
            Args args (command, end_options - command);
            if (!ParseOptions (args, result))
                return false;
            
            Error error (m_option_group.NotifyOptionParsingFinished());
            if (error.Fail())
            {
                result.AppendError (error.AsCString());
                result.SetStatus (eReturnStatusFailed);
                return false;
            }
        }
    }

    if (expr == NULL)
        expr = command;
    
    if (EvaluateExpression (expr, &(result.GetOutputStream()), &(result.GetErrorStream()), &result))
        return true;

    result.SetStatus (eReturnStatusFailed);
    return false;
}

