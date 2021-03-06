//===------------------SharedCluster.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_SharedCluster_h_
#define utility_SharedCluster_h_

#include "lldb/Utility/SharingPtr.h"
#include "lldb/Host/Mutex.h"

namespace lldb_private {

namespace imp
{
    template <typename T>
    class shared_ptr_refcount : public lldb_private::imp::shared_count
    {
    public:
        template<class Y> shared_ptr_refcount (Y *in) : shared_count (0), manager(in) {}
        
        shared_ptr_refcount() : shared_count (0) {}
        
        virtual ~shared_ptr_refcount ()
        {
        }
        
        virtual void on_zero_shared ()
        {
            manager->DecrementRefCount();
        }
    private:
        T *manager;
    };

} // namespace imp

template <class T>
class ClusterManager
{
public:
    ClusterManager () : 
        m_objects(),
        m_external_ref(0),
        m_mutex(Mutex::eMutexTypeNormal) {}
    
    ~ClusterManager ()
    {
        size_t n_items = m_objects.size();
        for (size_t i = 0; i < n_items; i++)
        {
            delete m_objects[i];
        }
    }
    
    void ManageObject (T *new_object)
    {
        m_mutex.Lock();
        if (!ContainsObject(new_object))
            m_objects.push_back (new_object);
        m_mutex.Unlock();
    }
    
    typename lldb_private::SharingPtr<T> GetSharedPointer(T *desired_object)
    {
        m_mutex.Lock();
        m_external_ref++;
        assert (ContainsObject(desired_object));
        m_mutex.Unlock();
        
        return typename lldb_private::SharingPtr<T> (desired_object, new imp::shared_ptr_refcount<ClusterManager> (this));
    }
    
private:
    
    bool ContainsObject (const T *desired_object)
    {
        typename std::vector<T *>::iterator pos;
        pos = std::find(m_objects.begin(), m_objects.end(), desired_object);
        
        return pos < m_objects.end();
    }
    
    void DecrementRefCount () 
    {
        m_mutex.Lock();
        m_external_ref--;
        if (m_external_ref == 0)
            delete this;
        else
            m_mutex.Unlock();
    }
    
    friend class imp::shared_ptr_refcount<ClusterManager>;
    
    std::vector<T *> m_objects;
    int m_external_ref;
    Mutex m_mutex;
};

} // namespace lldb_private
#endif // utility_SharedCluster_h_
