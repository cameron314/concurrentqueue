/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_HISTORY_HPP
#define RL_HISTORY_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


typedef void (*event_output_f)(std::ostream& s, void const* ev);
typedef void (*event_dtor_f)(void* ev);

struct history_entry
{
    thread_id_t thread_index_;
    debug_info info_;
    void* ev_;
    event_output_f output_;
    event_dtor_f dtor_;

    history_entry(thread_id_t thread_index, debug_info_param info, void* ev, event_output_f output, event_dtor_f dtor)
        : thread_index_(thread_index)
        , info_(info)
        , ev_(ev)
        , output_(output)
        , dtor_(dtor)
    {
    }
};

template<typename T>
void event_output(std::ostream& s, void const* ev)
{
    static_cast<T const*>(ev)->output(s);
}

template<typename T>
void event_dtor(void* ev)
{
    delete static_cast<T*>(ev);
}


struct user_event
{
    char const* desc_;

    void output(std::ostream& s) const
    {
        s << desc_;
    }
};

inline string strip_path(char const* filename)
{
    char const* slash = strrchr(filename, '\\');
    if (slash)
        return slash + 1;
    else
        return filename;
}

inline std::ostream& operator << (std::ostream& ss, debug_info_param info)
{
    /*
    char const* func = info;
    char const* file = info + strlen(info) + 1;
    char const* line = file + strlen(file) + 1;
    */

#ifdef RL_MSVC_OUTPUT
    ss << info.file_ << "(" << info.line_ << ") : ";
#else
    ss << info.func_ << ", " << strip_path(info.file_) << "(" << info.line_ << ")";
#endif
    return ss;
}



class history_mgr : nocopy<>
{
public:
    history_mgr(std::ostream& stream, thread_id_t thread_count)
        : thread_count_(thread_count)
        , out_stream_(stream)
    {
    }

    ~history_mgr()
    {
        clear();
    }

    template<typename event_t>
    void exec_log(thread_id_t th, debug_info_param info, event_t const& ev, bool output_history)
    {
        exec_history_.push_back(history_entry(th, info, new event_t(ev), &event_output<event_t>, &event_dtor<event_t>));
        if (output_history)
        {
            output(exec_history_.size() - 1);
        }
    }

    void print_exec_history(bool output_history)
    {
        size_t const buf_size = 4096;
        char buf [buf_size + 1];

        size_t const count = exec_history_.size();
        if (false == output_history)
        {
            sprintf(buf, "execution history (%u):\n", (unsigned)count);
            out_stream_ << buf;
#if defined(_MSC_VER) && defined(RL_MSVC_OUTPUT)
            OutputDebugStringA(buf);
#endif

            for (size_t i = 0; i != count; ++i)
            {
                output(i);
            }
        }
        out_stream_ << "\n";
#if defined(_MSC_VER) && defined(RL_MSVC_OUTPUT)
        OutputDebugStringA("\n");
#endif

        for (thread_id_t th = 0; th != thread_count_; ++th)
        {
            sprintf(buf, "thread %u:\n", th);
            out_stream_ << buf;
#if defined(_MSC_VER) && defined(RL_MSVC_OUTPUT)
            OutputDebugStringA(buf);
#endif
            for (size_t i = 0; i != count; ++i)
            {
                if (exec_history_[i].thread_index_ == th)
                {
                    output(i);
                }
            }
            out_stream_ << "\n";
#if defined(_MSC_VER) && defined(RL_MSVC_OUTPUT)
            OutputDebugStringA("\n");
#endif
        }
    }

    void clear()
    {
        for (size_t i = 0; i != exec_history_.size(); ++i)
        {
            history_entry const& ent = exec_history_[i];
            ent.dtor_(ent.ev_);
        }
        exec_history_.clear();
    }

private:
    vector<history_entry>::type exec_history_;
    thread_id_t                 thread_count_;
    std::ostream&               out_stream_;

    void output(size_t i)
    {
        std::basic_ostringstream<char, std::char_traits<char>, raw_allocator<char> > stream;

        history_entry const& ent = exec_history_[i];
#ifdef RL_MSVC_OUTPUT
        {
            stream << ent.info_ << "[" << i << "] " << ent.thread_index_ << ": ";
            ent.output_(stream, ent.ev_);
            stream << std::endl;
        }
#else
        stream << "[" << (unsigned)i << "] " << ent.thread_index_ << ": ";
        ent.output_(stream, ent.ev_);
        stream << ", in " << ent.info_ << std::endl;
#endif

        out_stream_ << stream.str();
#if defined(_MSC_VER) && defined(RL_MSVC_OUTPUT)
        OutputDebugStringA(stream.str().c_str());
#endif
    }
};


}

#endif
