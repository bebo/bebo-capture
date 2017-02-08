#ifndef G2FUTURE_H
#define G2FUTURE_H
/** ==========================================================================
* 2012 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
* with no warranties. This code is yours to share, use and modify with no
* strings attached and no restrictions or obligations.
* ============================================================================
* PUBLIC DOMAIN and NOT under copywrite protection.
* ********************************************* */



#include <future>
#include "active.h"
#include "g2moveoncopy.hpp"
namespace g2 {
// Generic helper function to avoid repeating the steps for managing
// asynchronous task job (by active object) that returns a future results
// could of course be made even more generic if done more in the way of
// std::async, ref: http://en.cppreference.com/w/cpp/thread/async
//
// Example usage:
//  std::unique_ptr<Active> bgWorker{Active::createActive()};
//  ...
//  auto msg_call=[=](){return ("Hello from the Background");};
//  auto future_msg = g2::spawn_task(msg_lambda, bgWorker.get());

template <typename Func>
std::future<typename std::result_of<Func()>::type> spawn_task(Func func, kjellkod::Active* worker) {
   typedef typename std::result_of<Func()>::type result_type;
   typedef std::packaged_task<result_type()> task_type;
   task_type task(std::move(func));
   std::future<result_type> result = task.get_future();

   worker->send(MoveOnCopy<task_type>(std::move(task)));
   return std::move(result);
}

} // end namespace g2
#endif // G2FUTURE_H

