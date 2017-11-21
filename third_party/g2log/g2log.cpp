/** ==========================================================================
 * 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
 * with no warranties. This code is yours to share, use and modify with no
 * strings attached and no restrictions or obligations.
 * ============================================================================
 *
 * Filename:g2log.cpp  Framework for Logging and Design By Contract
 * Created: 2011 by Kjell Hedström
 *
 * PUBLIC DOMAIN and Not copywrited since it was built on public-domain software and at least in "spirit" influenced
 * from the following sources
 * 1. kjellkod.cc ;)
 * 2. Dr.Dobbs, Petru Marginean:  http://drdobbs.com/article/printableArticle.jhtml?articleId=201804215&dept_url=/cpp/
 * 3. Dr.Dobbs, Michael Schulze: http://drdobbs.com/article/printableArticle.jhtml?articleId=225700666&dept_url=/cpp/
 * 4. Google 'glog': http://google-glog.googlecode.com/svn/trunk/doc/glog.html
 * 5. Various Q&A at StackOverflow
 * ********************************************* */

#include "g2log.h"

#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept> // exceptions
#include <cstdio>    // vsnprintf
#include <cassert>
#include <mutex>
#include <chrono>
#include <signal.h>
#include <thread>
#include <windows.h>

#include "g2logworker.h"
#include "crashhandler.h"

namespace {
std::once_flag g_initialize_flag;
g2LogWorker* g_logger_instance = nullptr; // instantiated and OWNED somewhere else (main)
std::mutex g_logging_init_mutex;

g2::internal::LogEntry g_first_unintialized_msg = {"", std::chrono::high_resolution_clock::now()};
std::once_flag g_set_first_uninitialized_flag;
std::once_flag g_save_first_unintialized_flag;


/** thanks to: http://www.cplusplus.com/reference/string/string/find_last_of/
* Splits string at the last '/' or '\\' separator
* example: "/mnt/something/else.cpp" --> "else.cpp"
*          "c:\\windows\\hello.h" --> hello.h
*          "this.is.not-a-path.h" -->"this.is.not-a-path.h" */
std::string splitFileName(const std::string& str) {
   size_t found;
   found = str.find_last_of("(/\\");
   return str.substr(found + 1);
}

const int kMaxMessageSize = 2048;
const std::string kTruncatedWarningText = "[...truncated...]";




void saveToLogger(const g2::internal::LogEntry& log_entry) {
   // Uninitialized messages are ignored but does not CHECK/crash the logger
   if (!g2::internal::isLoggingInitialized()) {
      std::string err("LOGGER NOT INITIALIZED: " + log_entry.msg_);
      std::call_once(g_set_first_uninitialized_flag,
                     [&] { g_first_unintialized_msg.msg_ += err;
                           g_first_unintialized_msg.timestamp_ = std::chrono::high_resolution_clock::now();
                         });
      // dump to std::err all the non-initialized logs
      std::cerr << err << std::endl;
      return;
   }
   // Save the first uninitialized message, if any
   std::call_once(g_save_first_unintialized_flag, [] {
      if (!g_first_unintialized_msg.msg_.empty()) {
         g_logger_instance->save(g_first_unintialized_msg);
      }
   });

   g_logger_instance->save(log_entry);
}
} // anonymous


namespace g2 {


// signalhandler and internal clock is only needed to install once
// for unit testing purposes the initializeLogging might be called
// several times... for all other practical use, it shouldn't!
void initializeLogging(g2LogWorker* bgworker) {
   std::call_once(g_initialize_flag, []() {
      installSignalHandler();
   });

   std::lock_guard<std::mutex> lock(g_logging_init_mutex);
   CHECK(!internal::isLoggingInitialized());
   CHECK(bgworker != nullptr);
   g_logger_instance = bgworker;
}



void  shutDownLogging() {
   std::lock_guard<std::mutex> lock(g_logging_init_mutex);
   g_logger_instance = nullptr;
}



bool shutDownLoggingForActiveOnly(g2LogWorker* active) {
   if (internal::isLoggingInitialized() && nullptr != active  &&
         (dynamic_cast<void*>(active) != dynamic_cast<void*>(g_logger_instance))) {
      LOG(WARNING) << "\n\t\tShutting down logging, but the ID of the Logger is not the one that is active."
                   << "\n\t\tHaving multiple instances of the g2::LogWorker is likely a BUG"
                   << "\n\t\tEither way, this call to shutDownLogging was ignored";
      return false;
   }
   shutDownLogging();
   return true;
}



namespace internal {

g2::high_resolution_time_point highresolution_clock_now() {
   return std::chrono::high_resolution_clock::now();
}

bool isLoggingInitialized() {
   return g_logger_instance != nullptr;
}


/** Fatal call saved to logger. This will trigger SIGABRT or other fatal signal
  * to exit the program. After saving the fatal message the calling thread
  * will sleep forever (i.e. until the background thread catches up, saves the fatal
  * message and kills the software with the fatal signal.
*/
void fatalCallToLogger(FatalMessage message) {
   if (!isLoggingInitialized()) {
      std::ostringstream error;
      error << "FATAL CALL but logger is NOT initialized\n"
            << "SIGNAL: " << g2::internal::signalName(message.signal_id_)
            << "\nMessage: \n" << message.message_.msg_ << std::flush;
      std::cerr << error.str();

      internal::exitWithDefaultSignalHandler(message.signal_id_);
   }
   g_logger_instance->fatal(message);
   while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
   }
}

// By default this function pointer goes to \ref callFatalInitial;
std::function<void(FatalMessage) > g_fatal_to_g2logworker_function_ptr = fatalCallToLogger;

// In case of unit-testing - a replacement 'fatal function' can be called
void changeFatalInitHandlerForUnitTesting(std::function<void(FatalMessage) > fatal_call) {
   g_fatal_to_g2logworker_function_ptr = fatal_call;
}





LogContractMessage::LogContractMessage(const std::string& file, const int line,
                                       const std::string& function, const std::string& boolean_expression)
   : LogMessage(file, line, function, "FATAL")
   , expression_(boolean_expression)
{}

LogContractMessage::~LogContractMessage() {
   std::ostringstream oss;
   if (0 == expression_.compare(k_fatal_log_expression)) {
      oss << "\n[  *******\tEXIT trigger caused by LOG(FATAL): \n\t";
   } else {
      oss << "\n[  *******\tEXIT trigger caused by broken Contract: CHECK(" << expression_ << ")\n\t";
   }
   log_entry_ = oss.str();
}

LogMessage::LogMessage(const std::string& file, const int line, const std::string& function, const std::string& level)
   : file_(file)
   , line_(line)
   , function_(function)
   , level_(level)
   , timestamp_(std::chrono::high_resolution_clock::now())
{}


LogMessage::~LogMessage() {
   using namespace internal;
   std::ostringstream oss;
   const bool fatal = (0 == level_.compare("FATAL"));
   oss << level_ << " [" << splitFileName(file_);
   if (fatal)
      oss <<  " at: " << function_ ;
   oss << " L: " << line_ << "]\t";

   const std::string str(stream_.str());
   if (!str.empty()) {
      oss << '"' << str << '"';
   }
   log_entry_ += oss.str();


   if (fatal) { // os_fatal is handled by crashhandlers
      {
         // local scope - to trigger FatalMessage sending
         FatalMessage::FatalType fatal_type(FatalMessage::kReasonFatal);
         FatalMessage fatal_message({log_entry_, timestamp_}, fatal_type, SIGABRT);
         FatalTrigger trigger(fatal_message);
         std::cerr  << log_entry_ << "\t*******  ]" << std::endl << std::flush;
      } // will send to worker
   }
   saveToLogger({log_entry_, timestamp_}); // message saved
}


// represents the actual fatal message
FatalMessage::FatalMessage(LogEntry message, FatalType type, int signal_id)
   : message_(message)
   , type_(type)
   , signal_id_(signal_id) {}


FatalMessage& FatalMessage::operator=(const FatalMessage& other) {
   message_ = other.message_;
   type_ = other.type_;
   signal_id_ = other.signal_id_;
   return *this;
}


// used to RAII trigger fatal message sending to g2LogWorker
FatalTrigger::FatalTrigger(const FatalMessage& message)
   :  message_(message) {}

// at destruction, flushes fatal message to g2LogWorker
FatalTrigger::~FatalTrigger() {
   // either we will stay here eternally, or it's in unit-test mode
   g_fatal_to_g2logworker_function_ptr(message_);

}



void LogMessage::messageSave(const wchar_t* printf_like_message, ...) {
   wchar_t finished_message[kMaxMessageSize];
   va_list arglist;
   va_start(arglist, printf_like_message);
   const int nbrcharacters = _vsnwprintf(finished_message, sizeof(finished_message), printf_like_message, arglist);
   va_end(arglist);

   if (nbrcharacters <= 0) {
	   stream_ << "\n\tERROR LOG MSG NOTIFICATION: Failure to parse successfully the message";
	   stream_ << '"' << printf_like_message << '"' << std::endl;
   } else if (nbrcharacters > kMaxMessageSize) {
	   stream_ << finished_message << kTruncatedWarningText;
   } else {
	   char data[kMaxMessageSize] = { 0 };
	   int datasize;
	   datasize = WideCharToMultiByte(CP_UTF8, 0, finished_message, kMaxMessageSize, data, kMaxMessageSize, NULL, NULL);
	   stream_ << data;
   }
}

} // end of namespace g2::internal
} // end of namespace g2
