//===-- Thread.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private-log.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Stream.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Target/ThreadPlanBase.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepOverBreakpoint.h"
#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/ThreadPlanStepOverRange.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Target/ThreadPlanStepUntil.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Target/Unwind.h"
#include "Plugins/Process/Utility/UnwindLLDB.h"
#include "UnwindMacOSXFrameBackchain.h"


using namespace lldb;
using namespace lldb_private;

Thread::Thread (Process &process, lldb::tid_t tid) :
    UserID (tid),
    ThreadInstanceSettings (*GetSettingsController()),
    m_process (process),
    m_actual_stop_info_sp (),
    m_index_id (process.GetNextThreadIndexID ()),
    m_reg_context_sp (),
    m_state (eStateUnloaded),
    m_state_mutex (Mutex::eMutexTypeRecursive),
    m_plan_stack (),
    m_completed_plan_stack(),
    m_curr_frames_sp (),
    m_prev_frames_sp (),
    m_resume_signal (LLDB_INVALID_SIGNAL_NUMBER),
    m_resume_state (eStateRunning),
    m_unwinder_ap (),
    m_destroy_called (false),
    m_thread_stop_reason_stop_id (0)

{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_OBJECT));
    if (log)
        log->Printf ("%p Thread::Thread(tid = 0x%4.4llx)", this, GetID());

    QueueFundamentalPlan(true);
    UpdateInstanceName();
}


Thread::~Thread()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_OBJECT));
    if (log)
        log->Printf ("%p Thread::~Thread(tid = 0x%4.4llx)", this, GetID());
    /// If you hit this assert, it means your derived class forgot to call DoDestroy in its destructor.
    assert (m_destroy_called);
}

void 
Thread::DestroyThread ()
{
    m_plan_stack.clear();
    m_discarded_plan_stack.clear();
    m_completed_plan_stack.clear();
    m_destroy_called = true;
}

lldb::StopInfoSP
Thread::GetStopInfo ()
{
    ThreadPlanSP plan_sp (GetCompletedPlan());
    if (plan_sp)
        return StopInfo::CreateStopReasonWithPlan (plan_sp, GetReturnValueObject());
    else
    {
        if (m_actual_stop_info_sp 
            && m_actual_stop_info_sp->IsValid()
            && m_thread_stop_reason_stop_id == m_process.GetStopID())
            return m_actual_stop_info_sp;
        else
            return GetPrivateStopReason ();
    }
}

void
Thread::SetStopInfo (const lldb::StopInfoSP &stop_info_sp)
{
    m_actual_stop_info_sp = stop_info_sp;
    if (m_actual_stop_info_sp)
        m_actual_stop_info_sp->MakeStopInfoValid();
    m_thread_stop_reason_stop_id = GetProcess().GetStopID();
}

void
Thread::SetStopInfoToNothing()
{
    // Note, we can't just NULL out the private reason, or the native thread implementation will try to
    // go calculate it again.  For now, just set it to a Unix Signal with an invalid signal number.
    SetStopInfo (StopInfo::CreateStopReasonWithSignal (*this,  LLDB_INVALID_SIGNAL_NUMBER));
}

bool
Thread::ThreadStoppedForAReason (void)
{
    return GetPrivateStopReason () != NULL;
}

bool
Thread::CheckpointThreadState (ThreadStateCheckpoint &saved_state)
{
    if (!SaveFrameZeroState(saved_state.register_backup))
        return false;

    saved_state.stop_info_sp = GetStopInfo();
    saved_state.orig_stop_id = GetProcess().GetStopID();

    return true;
}

bool
Thread::RestoreThreadStateFromCheckpoint (ThreadStateCheckpoint &saved_state)
{
    RestoreSaveFrameZero(saved_state.register_backup);
    if (saved_state.stop_info_sp)
        saved_state.stop_info_sp->MakeStopInfoValid();
    SetStopInfo(saved_state.stop_info_sp);
    return true;
}

StateType
Thread::GetState() const
{
    // If any other threads access this we will need a mutex for it
    Mutex::Locker locker(m_state_mutex);
    return m_state;
}

void
Thread::SetState(StateType state)
{
    Mutex::Locker locker(m_state_mutex);
    m_state = state;
}

void
Thread::WillStop()
{
    ThreadPlan *current_plan = GetCurrentPlan();

    // FIXME: I may decide to disallow threads with no plans.  In which
    // case this should go to an assert.

    if (!current_plan)
        return;

    current_plan->WillStop();
}

void
Thread::SetupForResume ()
{
    if (GetResumeState() != eStateSuspended)
    {
    
        // If we're at a breakpoint push the step-over breakpoint plan.  Do this before
        // telling the current plan it will resume, since we might change what the current
        // plan is.

        lldb::addr_t pc = GetRegisterContext()->GetPC();
        BreakpointSiteSP bp_site_sp = GetProcess().GetBreakpointSiteList().FindByAddress(pc);
        if (bp_site_sp && bp_site_sp->IsEnabled())
        {
            // Note, don't assume there's a ThreadPlanStepOverBreakpoint, the target may not require anything
            // special to step over a breakpoint.
                
            ThreadPlan *cur_plan = GetCurrentPlan();

            if (cur_plan->GetKind() != ThreadPlan::eKindStepOverBreakpoint)
            {
                ThreadPlanStepOverBreakpoint *step_bp_plan = new ThreadPlanStepOverBreakpoint (*this);
                if (step_bp_plan)
                {
                    ThreadPlanSP step_bp_plan_sp;
                    step_bp_plan->SetPrivate (true);

                    if (GetCurrentPlan()->RunState() != eStateStepping)
                    {
                        step_bp_plan->SetAutoContinue(true);
                    }
                    step_bp_plan_sp.reset (step_bp_plan);
                    QueueThreadPlan (step_bp_plan_sp, false);
                }
            }
        }
    }
}

bool
Thread::WillResume (StateType resume_state)
{
    // At this point clear the completed plan stack.
    m_completed_plan_stack.clear();
    m_discarded_plan_stack.clear();

    StopInfo *stop_info = GetPrivateStopReason().get();
    if (stop_info)
        stop_info->WillResume (resume_state);
    
    // Tell all the plans that we are about to resume in case they need to clear any state.
    // We distinguish between the plan on the top of the stack and the lower
    // plans in case a plan needs to do any special business before it runs.
    
    ThreadPlan *plan_ptr = GetCurrentPlan();
    plan_ptr->WillResume(resume_state, true);

    while ((plan_ptr = GetPreviousPlan(plan_ptr)) != NULL)
    {
        plan_ptr->WillResume (resume_state, false);
    }
    
    m_actual_stop_info_sp.reset();
    return true;
}

void
Thread::DidResume ()
{
    SetResumeSignal (LLDB_INVALID_SIGNAL_NUMBER);
}

bool
Thread::ShouldStop (Event* event_ptr)
{
    ThreadPlan *current_plan = GetCurrentPlan();
    bool should_stop = true;

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (log)
    {
        log->Printf ("^^^^^^^^ Thread::ShouldStop Begin ^^^^^^^^");
        StreamString s;
        s.IndentMore();
        DumpThreadPlans(&s);
        log->Printf ("Plan stack initial state:\n%s", s.GetData());
    }
    
    // The top most plan always gets to do the trace log...
    current_plan->DoTraceLog ();

    // If the base plan doesn't understand why we stopped, then we have to find a plan that does.
    // If that plan is still working, then we don't need to do any more work.  If the plan that explains 
    // the stop is done, then we should pop all the plans below it, and pop it, and then let the plans above it decide
    // whether they still need to do more work.
    
    bool done_processing_current_plan = false;
    
    if (!current_plan->PlanExplainsStop())
    {
        if (current_plan->TracerExplainsStop())
        {
            done_processing_current_plan = true;
            should_stop = false;
        }
        else
        {
            // If the current plan doesn't explain the stop, then, find one that
            // does and let it handle the situation.
            ThreadPlan *plan_ptr = current_plan;
            while ((plan_ptr = GetPreviousPlan(plan_ptr)) != NULL)
            {
                if (plan_ptr->PlanExplainsStop())
                {
                    should_stop = plan_ptr->ShouldStop (event_ptr);
                    
                    // plan_ptr explains the stop, next check whether plan_ptr is done, if so, then we should take it 
                    // and all the plans below it off the stack.
                    
                    if (plan_ptr->MischiefManaged())
                    {
                        // We're going to pop the plans up to AND INCLUDING the plan that explains the stop.
                        plan_ptr = GetPreviousPlan(plan_ptr);
                        
                        do 
                        {
                            if (should_stop)
                                current_plan->WillStop();
                            PopPlan();
                        }
                        while ((current_plan = GetCurrentPlan()) != plan_ptr);
                        done_processing_current_plan = false;
                    }
                    else
                        done_processing_current_plan = true;
                        
                    break;
                }

            }
        }
    }
    
    if (!done_processing_current_plan)
    {
        bool over_ride_stop = current_plan->ShouldAutoContinue(event_ptr);
        
        if (log)
            log->Printf("Plan %s explains stop, auto-continue %i.", current_plan->GetName(), over_ride_stop);
            
        // We're starting from the base plan, so just let it decide;
        if (PlanIsBasePlan(current_plan))
        {
            should_stop = current_plan->ShouldStop (event_ptr);
            if (log)
                log->Printf("Base plan says should stop: %i.", should_stop);
        }
        else
        {
            // Otherwise, don't let the base plan override what the other plans say to do, since
            // presumably if there were other plans they would know what to do...
            while (1)
            {
                if (PlanIsBasePlan(current_plan))
                    break;
                    
                should_stop = current_plan->ShouldStop(event_ptr);
                if (log)
                    log->Printf("Plan %s should stop: %d.", current_plan->GetName(), should_stop);
                if (current_plan->MischiefManaged())
                {
                    if (should_stop)
                        current_plan->WillStop();

                    // If a Master Plan wants to stop, and wants to stick on the stack, we let it.
                    // Otherwise, see if the plan's parent wants to stop.

                    if (should_stop && current_plan->IsMasterPlan() && !current_plan->OkayToDiscard())
                    {
                        PopPlan();
                        break;
                    }
                    else
                    {

                        PopPlan();

                        current_plan = GetCurrentPlan();
                        if (current_plan == NULL)
                        {
                            break;
                        }
                    }

                }
                else
                {
                    break;
                }
            }
        }
        if (over_ride_stop)
            should_stop = false;
    }

    if (log)
    {
        StreamString s;
        s.IndentMore();
        DumpThreadPlans(&s);
        log->Printf ("Plan stack final state:\n%s", s.GetData());
        log->Printf ("vvvvvvvv Thread::ShouldStop End (returning %i) vvvvvvvv", should_stop);
    }
    return should_stop;
}

Vote
Thread::ShouldReportStop (Event* event_ptr)
{
    StateType thread_state = GetResumeState ();
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));

    if (thread_state == eStateSuspended || thread_state == eStateInvalid)
    {
        if (log)
            log->Printf ("Thread::ShouldReportStop() tid = 0x%4.4llx: returning vote %i (state was suspended or invalid)\n", GetID(), eVoteNoOpinion);
        return eVoteNoOpinion;
    }

    if (m_completed_plan_stack.size() > 0)
    {
        // Don't use GetCompletedPlan here, since that suppresses private plans.
        if (log)
            log->Printf ("Thread::ShouldReportStop() tid = 0x%4.4llx: returning vote  for complete stack's back plan\n", GetID());
        return m_completed_plan_stack.back()->ShouldReportStop (event_ptr);
    }
    else
    {
        if (log)
            log->Printf ("Thread::ShouldReportStop() tid = 0x%4.4llx: returning vote  for current plan\n", GetID());
        return GetCurrentPlan()->ShouldReportStop (event_ptr);
    }
}

Vote
Thread::ShouldReportRun (Event* event_ptr)
{
    StateType thread_state = GetResumeState ();
    
    if (thread_state == eStateSuspended
            || thread_state == eStateInvalid)
    {
        return eVoteNoOpinion;
    }
    
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (m_completed_plan_stack.size() > 0)
    {
        // Don't use GetCompletedPlan here, since that suppresses private plans.
        if (log)
            log->Printf ("Current Plan for thread %d (0x%4.4llx): %s being asked whether we should report run.", 
                         GetIndexID(), 
                         GetID(),
                         m_completed_plan_stack.back()->GetName());
                         
        return m_completed_plan_stack.back()->ShouldReportRun (event_ptr);
    }
    else
    {
        if (log)
            log->Printf ("Current Plan for thread %d (0x%4.4llx): %s being asked whether we should report run.", 
                         GetIndexID(), 
                         GetID(),
                         GetCurrentPlan()->GetName());
                         
        return GetCurrentPlan()->ShouldReportRun (event_ptr);
     }
}

bool
Thread::MatchesSpec (const ThreadSpec *spec)
{
    if (spec == NULL)
        return true;
        
    return spec->ThreadPassesBasicTests(this);    
}

void
Thread::PushPlan (ThreadPlanSP &thread_plan_sp)
{
    if (thread_plan_sp)
    {
        // If the thread plan doesn't already have a tracer, give it its parent's tracer:
        if (!thread_plan_sp->GetThreadPlanTracer())
            thread_plan_sp->SetThreadPlanTracer(m_plan_stack.back()->GetThreadPlanTracer());
        m_plan_stack.push_back (thread_plan_sp);
            
        thread_plan_sp->DidPush();

        LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
        if (log)
        {
            StreamString s;
            thread_plan_sp->GetDescription (&s, lldb::eDescriptionLevelFull);
            log->Printf("Pushing plan: \"%s\", tid = 0x%4.4llx.",
                        s.GetData(),
                        thread_plan_sp->GetThread().GetID());
        }
    }
}

void
Thread::PopPlan ()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));

    if (m_plan_stack.empty())
        return;
    else
    {
        ThreadPlanSP &plan = m_plan_stack.back();
        if (log)
        {
            log->Printf("Popping plan: \"%s\", tid = 0x%4.4llx.", plan->GetName(), plan->GetThread().GetID());
        }
        m_completed_plan_stack.push_back (plan);
        plan->WillPop();
        m_plan_stack.pop_back();
    }
}

void
Thread::DiscardPlan ()
{
    if (m_plan_stack.size() > 1)
    {
        ThreadPlanSP &plan = m_plan_stack.back();
        m_discarded_plan_stack.push_back (plan);
        plan->WillPop();
        m_plan_stack.pop_back();
    }
}

ThreadPlan *
Thread::GetCurrentPlan ()
{
    if (m_plan_stack.empty())
        return NULL;
    else
        return m_plan_stack.back().get();
}

ThreadPlanSP
Thread::GetCompletedPlan ()
{
    ThreadPlanSP empty_plan_sp;
    if (!m_completed_plan_stack.empty())
    {
        for (int i = m_completed_plan_stack.size() - 1; i >= 0; i--)
        {
            ThreadPlanSP completed_plan_sp;
            completed_plan_sp = m_completed_plan_stack[i];
            if (!completed_plan_sp->GetPrivate ())
            return completed_plan_sp;
        }
    }
    return empty_plan_sp;
}

ValueObjectSP
Thread::GetReturnValueObject ()
{
    if (!m_completed_plan_stack.empty())
    {
        for (int i = m_completed_plan_stack.size() - 1; i >= 0; i--)
        {
            ValueObjectSP return_valobj_sp;
            return_valobj_sp = m_completed_plan_stack[i]->GetReturnValueObject();
            if (return_valobj_sp)
            return return_valobj_sp;
        }
    }
    return ValueObjectSP();
}

bool
Thread::IsThreadPlanDone (ThreadPlan *plan)
{
    if (!m_completed_plan_stack.empty())
    {
        for (int i = m_completed_plan_stack.size() - 1; i >= 0; i--)
        {
            if (m_completed_plan_stack[i].get() == plan)
                return true;
        }
    }
    return false;
}

bool
Thread::WasThreadPlanDiscarded (ThreadPlan *plan)
{
    if (!m_discarded_plan_stack.empty())
    {
        for (int i = m_discarded_plan_stack.size() - 1; i >= 0; i--)
        {
            if (m_discarded_plan_stack[i].get() == plan)
                return true;
        }
    }
    return false;
}

ThreadPlan *
Thread::GetPreviousPlan (ThreadPlan *current_plan)
{
    if (current_plan == NULL)
        return NULL;

    int stack_size = m_completed_plan_stack.size();
    for (int i = stack_size - 1; i > 0; i--)
    {
        if (current_plan == m_completed_plan_stack[i].get())
            return m_completed_plan_stack[i-1].get();
    }

    if (stack_size > 0 && m_completed_plan_stack[0].get() == current_plan)
    {
        if (m_plan_stack.size() > 0)
            return m_plan_stack.back().get();
        else
            return NULL;
    }

    stack_size = m_plan_stack.size();
    for (int i = stack_size - 1; i > 0; i--)
    {
        if (current_plan == m_plan_stack[i].get())
            return m_plan_stack[i-1].get();
    }
    return NULL;
}

void
Thread::QueueThreadPlan (ThreadPlanSP &thread_plan_sp, bool abort_other_plans)
{
    if (abort_other_plans)
       DiscardThreadPlans(true);

    PushPlan (thread_plan_sp);
}


void
Thread::EnableTracer (bool value, bool single_stepping)
{
    int stack_size = m_plan_stack.size();
    for (int i = 0; i < stack_size; i++)
    {
        if (m_plan_stack[i]->GetThreadPlanTracer())
        {
            m_plan_stack[i]->GetThreadPlanTracer()->EnableTracing(value);
            m_plan_stack[i]->GetThreadPlanTracer()->EnableSingleStep(single_stepping);
        }
    }
}

void
Thread::SetTracer (lldb::ThreadPlanTracerSP &tracer_sp)
{
    int stack_size = m_plan_stack.size();
    for (int i = 0; i < stack_size; i++)
        m_plan_stack[i]->SetThreadPlanTracer(tracer_sp);
}

void
Thread::DiscardThreadPlansUpToPlan (lldb::ThreadPlanSP &up_to_plan_sp)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (log)
    {
        log->Printf("Discarding thread plans for thread tid = 0x%4.4llx, up to %p", GetID(), up_to_plan_sp.get());
    }

    int stack_size = m_plan_stack.size();
    
    // If the input plan is NULL, discard all plans.  Otherwise make sure this plan is in the
    // stack, and if so discard up to and including it.
    
    if (up_to_plan_sp.get() == NULL)
    {
        for (int i = stack_size - 1; i > 0; i--)
            DiscardPlan();
    }
    else
    {
        bool found_it = false;
        for (int i = stack_size - 1; i > 0; i--)
        {
            if (m_plan_stack[i] == up_to_plan_sp)
                found_it = true;
        }
        if (found_it)
        {
            bool last_one = false;
            for (int i = stack_size - 1; i > 0 && !last_one ; i--)
            {
                if (GetCurrentPlan() == up_to_plan_sp.get())
                    last_one = true;
                DiscardPlan();
            }
        }
    }
    return;
}

void
Thread::DiscardThreadPlans(bool force)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (log)
    {
        log->Printf("Discarding thread plans for thread (tid = 0x%4.4llx, force %d)", GetID(), force);
    }

    if (force)
    {
        int stack_size = m_plan_stack.size();
        for (int i = stack_size - 1; i > 0; i--)
        {
            DiscardPlan();
        }
        return;
    }

    while (1)
    {

        int master_plan_idx;
        bool discard;

        // Find the first master plan, see if it wants discarding, and if yes discard up to it.
        for (master_plan_idx = m_plan_stack.size() - 1; master_plan_idx >= 0; master_plan_idx--)
        {
            if (m_plan_stack[master_plan_idx]->IsMasterPlan())
            {
                discard = m_plan_stack[master_plan_idx]->OkayToDiscard();
                break;
            }
        }

        if (discard)
        {
            // First pop all the dependent plans:
            for (int i = m_plan_stack.size() - 1; i > master_plan_idx; i--)
            {

                // FIXME: Do we need a finalize here, or is the rule that "PrepareForStop"
                // for the plan leaves it in a state that it is safe to pop the plan
                // with no more notice?
                DiscardPlan();
            }

            // Now discard the master plan itself.
            // The bottom-most plan never gets discarded.  "OkayToDiscard" for it means
            // discard it's dependent plans, but not it...
            if (master_plan_idx > 0)
            {
                DiscardPlan();
            }
        }
        else
        {
            // If the master plan doesn't want to get discarded, then we're done.
            break;
        }

    }
}

ThreadPlan *
Thread::QueueFundamentalPlan (bool abort_other_plans)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanBase(*this));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForStepSingleInstruction
(
    bool step_over, 
    bool abort_other_plans, 
    bool stop_other_threads
)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanStepInstruction (*this, step_over, stop_other_threads, eVoteNoOpinion, eVoteNoOpinion));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForStepRange 
(
    bool abort_other_plans, 
    StepType type, 
    const AddressRange &range, 
    const SymbolContext &addr_context, 
    lldb::RunMode stop_other_threads,
    bool avoid_code_without_debug_info
)
{
    ThreadPlanSP thread_plan_sp;
    if (type == eStepTypeInto)
    {
        ThreadPlanStepInRange *plan = new ThreadPlanStepInRange (*this, range, addr_context, stop_other_threads);
        if (avoid_code_without_debug_info)
            plan->GetFlags().Set (ThreadPlanShouldStopHere::eAvoidNoDebug);
        else
            plan->GetFlags().Clear (ThreadPlanShouldStopHere::eAvoidNoDebug);
        thread_plan_sp.reset (plan);
    }
    else
        thread_plan_sp.reset (new ThreadPlanStepOverRange (*this, range, addr_context, stop_other_threads));

    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}


ThreadPlan *
Thread::QueueThreadPlanForStepOverBreakpointPlan (bool abort_other_plans)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanStepOverBreakpoint (*this));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForStepOut 
(
    bool abort_other_plans, 
    SymbolContext *addr_context, 
    bool first_insn,
    bool stop_other_threads, 
    Vote stop_vote, 
    Vote run_vote,
    uint32_t frame_idx
)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanStepOut (*this, 
                                                        addr_context, 
                                                        first_insn, 
                                                        stop_other_threads, 
                                                        stop_vote, 
                                                        run_vote, 
                                                        frame_idx));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForStepThrough (bool abort_other_plans, bool stop_other_threads)
{
    ThreadPlanSP thread_plan_sp(new ThreadPlanStepThrough (*this, stop_other_threads));
    if (!thread_plan_sp || !thread_plan_sp->ValidatePlan (NULL))
        return NULL;

    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForCallFunction (bool abort_other_plans,
                                        Address& function,
                                        lldb::addr_t arg,
                                        bool stop_other_threads,
                                        bool discard_on_error)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanCallFunction (*this, function, ClangASTType(), arg, stop_other_threads, discard_on_error));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForRunToAddress (bool abort_other_plans,
                                        Address &target_addr,
                                        bool stop_other_threads)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanRunToAddress (*this, target_addr, stop_other_threads));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();
}

ThreadPlan *
Thread::QueueThreadPlanForStepUntil (bool abort_other_plans,
                                     lldb::addr_t *address_list,
                                     size_t num_addresses,
                                     bool stop_other_threads,
                                     uint32_t frame_idx)
{
    ThreadPlanSP thread_plan_sp (new ThreadPlanStepUntil (*this, address_list, num_addresses, stop_other_threads, frame_idx));
    QueueThreadPlan (thread_plan_sp, abort_other_plans);
    return thread_plan_sp.get();

}

uint32_t
Thread::GetIndexID () const
{
    return m_index_id;
}

void
Thread::DumpThreadPlans (lldb_private::Stream *s) const
{
    uint32_t stack_size = m_plan_stack.size();
    int i;
    s->Indent();
    s->Printf ("Plan Stack for thread #%u: tid = 0x%4.4llx, stack_size = %d\n", GetIndexID(), GetID(), stack_size);
    for (i = stack_size - 1; i >= 0; i--)
    {
        s->IndentMore();
        s->Indent();
        s->Printf ("Element %d: ", i);
        m_plan_stack[i]->GetDescription (s, eDescriptionLevelFull);
        s->EOL();
        s->IndentLess();
    }

    stack_size = m_completed_plan_stack.size();
    if (stack_size > 0)
    {
        s->Indent();
        s->Printf ("Completed Plan Stack: %d elements.\n", stack_size);
        for (i = stack_size - 1; i >= 0; i--)
        {
            s->IndentMore();
            s->Indent();
            s->Printf ("Element %d: ", i);
            m_completed_plan_stack[i]->GetDescription (s, eDescriptionLevelFull);
            s->EOL();
            s->IndentLess();
        }
    }

    stack_size = m_discarded_plan_stack.size();
    if (stack_size > 0)
    {
        s->Indent();
        s->Printf ("Discarded Plan Stack: %d elements.\n", stack_size);
        for (i = stack_size - 1; i >= 0; i--)
        {
            s->IndentMore();
            s->Indent();
            s->Printf ("Element %d: ", i);
            m_discarded_plan_stack[i]->GetDescription (s, eDescriptionLevelFull);
            s->EOL();
            s->IndentLess();
        }
    }

}

Target *
Thread::CalculateTarget ()
{
    return m_process.CalculateTarget();
}

Process *
Thread::CalculateProcess ()
{
    return &m_process;
}

Thread *
Thread::CalculateThread ()
{
    return this;
}

StackFrame *
Thread::CalculateStackFrame ()
{
    return NULL;
}

void
Thread::CalculateExecutionContext (ExecutionContext &exe_ctx)
{
    m_process.CalculateExecutionContext (exe_ctx);
    exe_ctx.SetThreadPtr (this);
    exe_ctx.SetFramePtr (NULL);
}


StackFrameList &
Thread::GetStackFrameList ()
{
    if (!m_curr_frames_sp)
        m_curr_frames_sp.reset (new StackFrameList (*this, m_prev_frames_sp, true));
    return *m_curr_frames_sp;
}

void
Thread::ClearStackFrames ()
{
    if (m_curr_frames_sp && m_curr_frames_sp->GetNumFrames (false) > 1)
        m_prev_frames_sp.swap (m_curr_frames_sp);
    m_curr_frames_sp.reset();
}

lldb::StackFrameSP
Thread::GetFrameWithConcreteFrameIndex (uint32_t unwind_idx)
{
    return GetStackFrameList().GetFrameWithConcreteFrameIndex (unwind_idx);
}

void
Thread::DumpUsingSettingsFormat (Stream &strm, uint32_t frame_idx)
{
    ExecutionContext exe_ctx;
    StackFrameSP frame_sp;
    SymbolContext frame_sc;
    CalculateExecutionContext (exe_ctx);

    if (frame_idx != LLDB_INVALID_INDEX32)
    {
        frame_sp = GetStackFrameAtIndex (frame_idx);
        if (frame_sp)
        {
            exe_ctx.SetFrameSP(frame_sp);
            frame_sc = frame_sp->GetSymbolContext(eSymbolContextEverything);
        }
    }

    const char *thread_format = GetProcess().GetTarget().GetDebugger().GetThreadFormat();
    assert (thread_format);
    const char *end = NULL;
    Debugger::FormatPrompt (thread_format, 
                            frame_sp ? &frame_sc : NULL,
                            &exe_ctx, 
                            NULL,
                            strm, 
                            &end);
}

lldb::ThreadSP
Thread::GetSP ()
{
    // This object contains an instrusive ref count base class so we can
    // easily make a shared pointer to this object
    return ThreadSP(this);
}


void
Thread::SettingsInitialize ()
{
    UserSettingsControllerSP &usc = GetSettingsController();
    usc.reset (new SettingsController);
    UserSettingsController::InitializeSettingsController (usc,
                                                          SettingsController::global_settings_table,
                                                          SettingsController::instance_settings_table);
                                                          
    // Now call SettingsInitialize() on each 'child' setting of Thread.
    // Currently there are none.
}

void
Thread::SettingsTerminate ()
{
    // Must call SettingsTerminate() on each 'child' setting of Thread before terminating Thread settings.
    // Currently there are none.
    
    // Now terminate Thread Settings.
    
    UserSettingsControllerSP &usc = GetSettingsController();
    UserSettingsController::FinalizeSettingsController (usc);
    usc.reset();
}

UserSettingsControllerSP &
Thread::GetSettingsController ()
{
    static UserSettingsControllerSP g_settings_controller;
    return g_settings_controller;
}

void
Thread::UpdateInstanceName ()
{
    StreamString sstr;
    const char *name = GetName();

    if (name && name[0] != '\0')
        sstr.Printf ("%s", name);
    else if ((GetIndexID() != 0) || (GetID() != 0))
        sstr.Printf ("0x%4.4x", GetIndexID());

    if (sstr.GetSize() > 0)
	Thread::GetSettingsController()->RenameInstanceSettings (GetInstanceName().AsCString(), sstr.GetData());
}

lldb::StackFrameSP
Thread::GetStackFrameSPForStackFramePtr (StackFrame *stack_frame_ptr)
{
    return GetStackFrameList().GetStackFrameSPForStackFramePtr (stack_frame_ptr);
}

const char *
Thread::StopReasonAsCString (lldb::StopReason reason)
{
    switch (reason)
    {
    case eStopReasonInvalid:      return "invalid";
    case eStopReasonNone:         return "none";
    case eStopReasonTrace:        return "trace";
    case eStopReasonBreakpoint:   return "breakpoint";
    case eStopReasonWatchpoint:   return "watchpoint";
    case eStopReasonSignal:       return "signal";
    case eStopReasonException:    return "exception";
    case eStopReasonPlanComplete: return "plan complete";
    }


    static char unknown_state_string[64];
    snprintf(unknown_state_string, sizeof (unknown_state_string), "StopReason = %i", reason);
    return unknown_state_string;
}

const char *
Thread::RunModeAsCString (lldb::RunMode mode)
{
    switch (mode)
    {
    case eOnlyThisThread:     return "only this thread";
    case eAllThreads:         return "all threads";
    case eOnlyDuringStepping: return "only during stepping";
    }

    static char unknown_state_string[64];
    snprintf(unknown_state_string, sizeof (unknown_state_string), "RunMode = %i", mode);
    return unknown_state_string;
}

size_t
Thread::GetStatus (Stream &strm, uint32_t start_frame, uint32_t num_frames, uint32_t num_frames_with_source)
{
    size_t num_frames_shown = 0;
    strm.Indent();
    strm.Printf("%c ", GetProcess().GetThreadList().GetSelectedThread().get() == this ? '*' : ' ');
    if (GetProcess().GetTarget().GetDebugger().GetUseExternalEditor())
    {
        StackFrameSP frame_sp = GetStackFrameAtIndex(start_frame);
        if (frame_sp)
        {
            SymbolContext frame_sc(frame_sp->GetSymbolContext (eSymbolContextLineEntry));
            if (frame_sc.line_entry.line != 0 && frame_sc.line_entry.file)
            {
                Host::OpenFileInExternalEditor (frame_sc.line_entry.file, frame_sc.line_entry.line);
            }
        }
    }
    
    DumpUsingSettingsFormat (strm, start_frame);
    
    if (num_frames > 0)
    {
        strm.IndentMore();
        
        const bool show_frame_info = true;
        const uint32_t source_lines_before = 3;
        const uint32_t source_lines_after = 3;
        strm.IndentMore ();
        num_frames_shown = GetStackFrameList ().GetStatus (strm, 
                                                           start_frame, 
                                                           num_frames, 
                                                           show_frame_info, 
                                                           num_frames_with_source,
                                                           source_lines_before,
                                                           source_lines_after);
        strm.IndentLess();
        strm.IndentLess();
    }
    return num_frames_shown;
}

size_t
Thread::GetStackFrameStatus (Stream& strm,
                             uint32_t first_frame,
                             uint32_t num_frames,
                             bool show_frame_info,
                             uint32_t num_frames_with_source,
                             uint32_t source_lines_before,
                             uint32_t source_lines_after)
{
    return GetStackFrameList().GetStatus (strm, 
                                          first_frame,
                                          num_frames,
                                          show_frame_info,
                                          num_frames_with_source,
                                          source_lines_before,
                                          source_lines_after);
}

bool
Thread::SaveFrameZeroState (RegisterCheckpoint &checkpoint)
{
    lldb::StackFrameSP frame_sp(GetStackFrameAtIndex (0));
    if (frame_sp)
    {
        checkpoint.SetStackID(frame_sp->GetStackID());
        return frame_sp->GetRegisterContext()->ReadAllRegisterValues (checkpoint.GetData());
    }
    return false;
}

bool
Thread::RestoreSaveFrameZero (const RegisterCheckpoint &checkpoint)
{
    lldb::StackFrameSP frame_sp(GetStackFrameAtIndex (0));
    if (frame_sp)
    {
        bool ret = frame_sp->GetRegisterContext()->WriteAllRegisterValues (checkpoint.GetData());

        // Clear out all stack frames as our world just changed.
        ClearStackFrames();
        frame_sp->GetRegisterContext()->InvalidateIfNeeded(true);

        return ret;
    }
    return false;
}

Unwind *
Thread::GetUnwinder ()
{
    if (m_unwinder_ap.get() == NULL)
    {
        const ArchSpec target_arch (GetProcess().GetTarget().GetArchitecture ());
        const llvm::Triple::ArchType machine = target_arch.GetMachine();
        switch (machine)
        {
            case llvm::Triple::x86_64:
            case llvm::Triple::x86:
            case llvm::Triple::arm:
            case llvm::Triple::thumb:
                m_unwinder_ap.reset (new UnwindLLDB (*this));
                break;
                
            default:
                if (target_arch.GetTriple().getVendor() == llvm::Triple::Apple)
                    m_unwinder_ap.reset (new UnwindMacOSXFrameBackchain (*this));
                break;
        }
    }
    return m_unwinder_ap.get();
}


#pragma mark "Thread::SettingsController"
//--------------------------------------------------------------
// class Thread::SettingsController
//--------------------------------------------------------------

Thread::SettingsController::SettingsController () :
    UserSettingsController ("thread", Process::GetSettingsController())
{
    m_default_settings.reset (new ThreadInstanceSettings (*this, false, 
                                                          InstanceSettings::GetDefaultName().AsCString()));
}

Thread::SettingsController::~SettingsController ()
{
}

lldb::InstanceSettingsSP
Thread::SettingsController::CreateInstanceSettings (const char *instance_name)
{
    ThreadInstanceSettings *new_settings = new ThreadInstanceSettings (*GetSettingsController(),
                                                                       false, 
                                                                       instance_name);
    lldb::InstanceSettingsSP new_settings_sp (new_settings);
    return new_settings_sp;
}

#pragma mark "ThreadInstanceSettings"
//--------------------------------------------------------------
// class ThreadInstanceSettings
//--------------------------------------------------------------

ThreadInstanceSettings::ThreadInstanceSettings (UserSettingsController &owner, bool live_instance, const char *name) :
    InstanceSettings (owner, name ? name : InstanceSettings::InvalidName().AsCString(), live_instance), 
    m_avoid_regexp_ap (),
    m_trace_enabled (false)
{
    // CopyInstanceSettings is a pure virtual function in InstanceSettings; it therefore cannot be called
    // until the vtables for ThreadInstanceSettings are properly set up, i.e. AFTER all the initializers.
    // For this reason it has to be called here, rather than in the initializer or in the parent constructor.
    // This is true for CreateInstanceName() too.
   
    if (GetInstanceName() == InstanceSettings::InvalidName())
    {
        ChangeInstanceName (std::string (CreateInstanceName().AsCString()));
        m_owner.RegisterInstanceSettings (this);
    }

    if (live_instance)
    {
        const lldb::InstanceSettingsSP &pending_settings = m_owner.FindPendingSettings (m_instance_name);
        CopyInstanceSettings (pending_settings,false);
        //m_owner.RemovePendingSettings (m_instance_name);
    }
}

ThreadInstanceSettings::ThreadInstanceSettings (const ThreadInstanceSettings &rhs) :
    InstanceSettings (*Thread::GetSettingsController(), CreateInstanceName().AsCString()),
    m_avoid_regexp_ap (),
    m_trace_enabled (rhs.m_trace_enabled)
{
    if (m_instance_name != InstanceSettings::GetDefaultName())
    {
        const lldb::InstanceSettingsSP &pending_settings = m_owner.FindPendingSettings (m_instance_name);
        CopyInstanceSettings (pending_settings,false);
        m_owner.RemovePendingSettings (m_instance_name);
    }
    if (rhs.m_avoid_regexp_ap.get() != NULL)
        m_avoid_regexp_ap.reset(new RegularExpression(rhs.m_avoid_regexp_ap->GetText()));
}

ThreadInstanceSettings::~ThreadInstanceSettings ()
{
}

ThreadInstanceSettings&
ThreadInstanceSettings::operator= (const ThreadInstanceSettings &rhs)
{
    if (this != &rhs)
    {
        if (rhs.m_avoid_regexp_ap.get() != NULL)
            m_avoid_regexp_ap.reset(new RegularExpression(rhs.m_avoid_regexp_ap->GetText()));
        else
            m_avoid_regexp_ap.reset(NULL);
    }
    m_trace_enabled = rhs.m_trace_enabled;
    return *this;
}


void
ThreadInstanceSettings::UpdateInstanceSettingsVariable (const ConstString &var_name,
                                                         const char *index_value,
                                                         const char *value,
                                                         const ConstString &instance_name,
                                                         const SettingEntry &entry,
                                                         VarSetOperationType op,
                                                         Error &err,
                                                         bool pending)
{
    if (var_name == StepAvoidRegexpVarName())
    {
        std::string regexp_text;
        if (m_avoid_regexp_ap.get() != NULL)
            regexp_text.append (m_avoid_regexp_ap->GetText());
        UserSettingsController::UpdateStringVariable (op, regexp_text, value, err);
        if (regexp_text.empty())
            m_avoid_regexp_ap.reset();
        else
        {
            m_avoid_regexp_ap.reset(new RegularExpression(regexp_text.c_str()));
            
        }
    }
    else if (var_name == GetTraceThreadVarName())
    {
        bool success;
        bool result = Args::StringToBoolean(value, false, &success);

        if (success)
        {
            m_trace_enabled = result;
            if (!pending)
            {
                Thread *myself = static_cast<Thread *> (this);
                myself->EnableTracer(m_trace_enabled, true);
            }
        }
        else
        {
            err.SetErrorStringWithFormat ("Bad value \"%s\" for trace-thread, should be Boolean.", value);
        }

    }
}

void
ThreadInstanceSettings::CopyInstanceSettings (const lldb::InstanceSettingsSP &new_settings,
                                               bool pending)
{
    if (new_settings.get() == NULL)
        return;

    ThreadInstanceSettings *new_process_settings = (ThreadInstanceSettings *) new_settings.get();
    if (new_process_settings->GetSymbolsToAvoidRegexp() != NULL)
        m_avoid_regexp_ap.reset (new RegularExpression (new_process_settings->GetSymbolsToAvoidRegexp()->GetText()));
    else 
        m_avoid_regexp_ap.reset ();
}

bool
ThreadInstanceSettings::GetInstanceSettingsValue (const SettingEntry &entry,
                                                  const ConstString &var_name,
                                                  StringList &value,
                                                  Error *err)
{
    if (var_name == StepAvoidRegexpVarName())
    {
        if (m_avoid_regexp_ap.get() != NULL)
        {
            std::string regexp_text("\"");
            regexp_text.append(m_avoid_regexp_ap->GetText());
            regexp_text.append ("\"");
            value.AppendString (regexp_text.c_str());
        }

    }
    else if (var_name == GetTraceThreadVarName())
    {
        value.AppendString(m_trace_enabled ? "true" : "false");
    }
    else
    {
        if (err)
            err->SetErrorStringWithFormat ("unrecognized variable name '%s'", var_name.AsCString());
        return false;
    }
    return true;
}

const ConstString
ThreadInstanceSettings::CreateInstanceName ()
{
    static int instance_count = 1;
    StreamString sstr;

    sstr.Printf ("thread_%d", instance_count);
    ++instance_count;

    const ConstString ret_val (sstr.GetData());
    return ret_val;
}

const ConstString &
ThreadInstanceSettings::StepAvoidRegexpVarName ()
{
    static ConstString step_avoid_var_name ("step-avoid-regexp");

    return step_avoid_var_name;
}

const ConstString &
ThreadInstanceSettings::GetTraceThreadVarName ()
{
    static ConstString trace_thread_var_name ("trace-thread");

    return trace_thread_var_name;
}

//--------------------------------------------------
// SettingsController Variable Tables
//--------------------------------------------------

SettingEntry
Thread::SettingsController::global_settings_table[] =
{
  //{ "var-name",    var-type  ,        "default", enum-table, init'd, hidden, "help-text"},
    {  NULL, eSetVarTypeNone, NULL, NULL, 0, 0, NULL }
};


SettingEntry
Thread::SettingsController::instance_settings_table[] =
{
  //{ "var-name",    var-type,              "default",      enum-table, init'd, hidden, "help-text"},
    { "step-avoid-regexp",  eSetVarTypeString,      "",  NULL,       false,  false,  "A regular expression defining functions step-in won't stop in." },
    { "trace-thread",  eSetVarTypeBoolean,      "false",  NULL,       false,  false,  "If true, this thread will single-step and log execution." },
    {  NULL, eSetVarTypeNone, NULL, NULL, 0, 0, NULL }
};
