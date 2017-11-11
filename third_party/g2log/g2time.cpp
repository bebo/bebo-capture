/** ==========================================================================
* 2012 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
* with no warranties. This code is yours to share, use and modify with no
* strings attached and no restrictions or obligations.
* ============================================================================
* Filename:g2time.cpp cross-platform, thread-safe replacement for C++11 non-thread-safe
*                   localtime (and similar)
* Created: 2012 by Kjell Hedström
*
* PUBLIC DOMAIN and Not under copywrite protection. First published for g2log at KjellKod.cc
* ********************************************* */

#include "g2time.h"

#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>


namespace g2 { namespace internal {
	const std::string kFractionalIdentier = "%f";
	const size_t kFractionalIdentierSize = 2;

	Fractional getFractional(const std::string& format_buffer, size_t pos) {
		char  ch = (format_buffer.size() > pos + kFractionalIdentierSize ? format_buffer.at(pos + kFractionalIdentierSize) : '\0');
		Fractional type = Fractional::NanosecondDefault;
		switch (ch) {
		case '3': type = Fractional::Millisecond; break;
		case '6': type = Fractional::Microsecond; break;
		case '9': type = Fractional::Nanosecond; break;
		default: type = Fractional::NanosecondDefault; break;
		}
		return type;
	}

	// Returns the fractional as a string with padded zeroes
	// 1 ms --> 001
	// 1 us --> 000001
	// 1 ns --> 000000001
	std::string to_string(const g2::system_time_point& ts, Fractional fractional) {
		auto duration = ts.time_since_epoch();
		auto sec_duration = std::chrono::duration_cast<std::chrono::seconds>(duration);
		duration -= sec_duration;
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

		auto zeroes = 9; // default ns
		auto digitsToCut = 1; // default ns, divide by 1 makes no change
		switch (fractional) {
		case Fractional::Millisecond: {
			zeroes = 3;
			digitsToCut = 1000000;
			break;
		}
		case Fractional::Microsecond: {
			zeroes = 6;
			digitsToCut = 1000;
			break;
		}
		case Fractional::Nanosecond:
		case Fractional::NanosecondDefault:
		default:
			zeroes = 9;
			digitsToCut = 1;

		}

		ns /= digitsToCut;
		auto value = std::string(std::to_string(ns));
		return std::string(zeroes - value.size(), '0') + value;
	}

	std::string localtime_formatted_fractions(const g2::system_time_point& ts, std::string format_buffer) {
		// iterating through every "%f" instance in the format string
		auto identifierExtraSize = 0;
		for (size_t pos = 0;
			(pos = format_buffer.find(g2::internal::kFractionalIdentier, pos)) != std::string::npos;
			pos += g2::internal::kFractionalIdentierSize + identifierExtraSize) {
			// figuring out whether this is nano, micro or milli identifier
			auto type = g2::internal::getFractional(format_buffer, pos);
			auto value = g2::internal::to_string(ts, type);
			auto padding = 0;
			if (type != g2::internal::Fractional::NanosecondDefault) {
				padding = 1;
			}

			// replacing "%f[3|6|9]" with sec fractional part value
			format_buffer.replace(pos, g2::internal::kFractionalIdentier.size() + padding, value);
		}
		return format_buffer;
	}

  // This mimics the original "std::put_time(const std::tm* tmb, const charT* fmt)"
  // This is needed since latest version (at time of writing) of gcc4.7 does not implement this library function yet.
  // return value is SIMPLIFIED to only return a std::string
  std::string put_time(const struct tm* tmb, const char* c_time_format)
  {
#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__)) && !defined(__MINGW32__)
    std::ostringstream oss;
    oss.fill('0');
    oss << std::put_time(const_cast<struct tm*>(tmb), c_time_format); // BOGUS hack done for VS2012: C++11 non-conformant since it SHOULD take a "const struct tm*  "
    return oss.str();
#else    // LINUX
    const size_t size = 1024;
    char buffer[size]; // IMPORTANT: check now and then for when gcc will implement std::put_time finns. 
    //                    ... also ... This is way more buffer space then we need
    auto success = std::strftime(buffer, size, c_time_format, tmb); 
    if (0 == success)
      return c_time_format; // For this hack it is OK but in case of more permanent we really should throw here, or even assert
    return buffer; 
#endif
  }
} // internal
} // g2



namespace g2
{
std::time_t systemtime_now()
{
  system_time_point system_now = std::chrono::system_clock::now();
  return std::chrono::system_clock::to_time_t(system_now);
}


tm gmtime(const std::time_t& time)
{
  struct tm tm_snapshot;
  gmtime_s(&tm_snapshot, &time); // windows
  return tm_snapshot;
}

/// returns a std::string with content of time_t as localtime formatted by input format string
/// * format string must conform to std::put_time
/// This is similar to std::put_time(std::localtime(std::time_t*), time_format.c_str());
std::string gmtime_formatted(const std::time_t& time_snapshot, const std::string& time_format)
{
  std::tm t = gmtime(time_snapshot); // could be const, but cannot due to VS2012 is non conformant for C++11's std::put_time (see above)
  std::stringstream buffer;
  buffer << g2::internal::put_time(&t, time_format.c_str());  // format example: //"%Y/%m/%d %H:%M:%S");
  return buffer.str();
}

std::string put_time(const struct tm* tmb, const char* c_time_format) {
	std::ostringstream oss;
	oss.fill('0');
	// BOGUS hack done for VS2012: C++11 non-conformant since it SHOULD take a "const struct tm*  "
	oss << std::put_time(const_cast<struct tm*> (tmb), c_time_format);
	return oss.str();
}

tm localtime(const std::time_t& ts) {
	struct tm tm_snapshot;
	localtime_s(&tm_snapshot, &ts); // windsows
	return tm_snapshot;
}

std::string localtime_formatted(const g2::system_time_point& ts, const std::string& time_format) {
	auto format_buffer = internal::localtime_formatted_fractions(ts, time_format);
	auto time_point = std::chrono::system_clock::to_time_t(ts);
	std::tm t = localtime(time_point);
	return g2::put_time(&t, format_buffer.c_str()); // format example: //"%Y/%m/%d %H:%M:%S");
}

std::string localtime_formatted(const g2::high_resolution_time_point& ts, const std::string& time_format) {
	return localtime_formatted(to_system_time(ts), time_format); // format example: //"%Y/%m/%d %H:%M:%S");
}
} // g2
