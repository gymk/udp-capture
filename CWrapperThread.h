
#ifndef __CWRAPPER_THREAD_H__
    #define __CWRAPPER_THREAD_H__

#include <pthread.h>

typedef void* (*CThreadFunc)(void *ctxt);

class CWrapperThread
{
public:
    CWrapperThread()
    {
        m_bThreadCreated = false;
    };

    ~CWrapperThread()
    {
        if(m_bThreadCreated)
        {
            StopThread();
        }
    };

    bool StartThread(CThreadFunc pFunc, void* pParam, const char* pui8ThreadName)
    {
        pthread_attr_t tThreadAttr;
        //struct sched_param tParam;
        
        /*creating thread to read section*/
        pthread_attr_init(&tThreadAttr);
        //pthread_attr_setschedpolicy(&tThreadAttr, SCHED_RR);
        //tParam.sched_priority = sched_get_priority_min(SCHED_RR) + 5;
        //pthread_attr_setschedparam(&tThreadAttr, &tParam);
        pthread_attr_setdetachstate(&tThreadAttr, PTHREAD_CREATE_JOINABLE);
        
        if(0 != pthread_create (&m_Thread, &tThreadAttr, pFunc, pParam))
        {
            printf("Cannot create thread for %s\n", pui8ThreadName);
        }
        else
        {
            m_bThreadCreated = true;
        }
        
        pthread_attr_destroy (&tThreadAttr);
    };

    void StopThread(void)
    {
        if(m_bThreadCreated)
        {
            void * retval=NULL;

            pthread_join (m_Thread, &retval);
            pthread_cancel (m_Thread);

            m_bThreadCreated = false;
        }
    };
    
private:
    pthread_t   m_Thread;
    bool        m_bThreadCreated;
};

#endif /* !__CWRAPPER_THREAD_H__ */

