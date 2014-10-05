// This is an attempt at bringing .NET-like format strings to C++.
//
// Tested with MSVC 12.0.
//
// Pros: Type-safe! Avoids catastrophic problems with:
// - Passing the wrong number of arguments to printf().
// - Passing the wrong type of argument for a particular printf() specifier.
// - Passing non-POD types into printf() (like using std::string with %s without .c_str())
// Other cool things you can't do with printf-style format strings:
// - Rearrangement! The format string indexes them, so you can put them in whatever order you want.
//
// Downsides with this particular approach:
// - Requires making a copy of each argument (to handle stack temporaries). This unfortunately
//   means that everything to print must be CopyConstructable. :/
// - To avoid slicing we allocate those copies on the heap.
//   (this could probably be fixed with small-buffer optimization)
//
// A format item has this syntax:
// {index[,alignment][:specifier[precision]]
//
// The default supported specifiers are:
//    d/D -> decimal
//    e/E -> exponential (lowercase/uppercase)
//    f/F -> fixed-point
//    o/O -> octal
//    x/X -> hexadecimal (lowercase/uppercase)
//
// By default, uses an object's operator<< for printing. Clients can specialize cppbits::print
// for more advanced behavior.
//
// USAGE:
//
//   You can get formatted strings with the following:
//      std::string test = cppbits::format("Test: {0}", 42);
//
//   You can also insert format strings into streams.
//      std::cout << cppbits::format("Test: {0:X}, {1}", 42, "sup") << std::endl;
//   This gets effectively treated as:
//      std::cout << "Test: " << [push!] std::uppercase << std::hex << 42 << [pop!]
//                << ", " << [push!] "sup" << [pop!]
//                << std::endl;
//
// ===============================================================================
// This file is released into the public domain. See LICENCE for more information.
// ===============================================================================

#include <string>
#include <iostream>
#include <sstream>
#include <utility>
#include <array>
#include <memory>
#include <iterator>
#include <iomanip>

namespace cppbits {

namespace detail {

// Saves the flags, precision, and width settings of a particular
// std::ostream; restores them on destruction.
class auto_stream_state
{
public:
	auto_stream_state(std::ostream& o) :
		stream(o),
		flags(o.flags()),
		precision(o.precision()),
		width(o.width())
	{}

	~auto_stream_state()
	{
		stream.width(width);
		stream.precision(precision);
		stream.flags(flags);
	}

private:
	std::ostream& stream;
	std::ios_base::fmtflags flags;
	std::streamsize precision;
	std::streamsize width;
};

static void default_format_handler(std::ostream& o, size_t width, char specifier, size_t precision)
{
	if (isupper(specifier))
		o << std::uppercase;
	else
		o << std::nouppercase;

	if (width)
		o << std::setw(width);
	if (precision)
		o << std::setprecision(precision);

	switch (tolower(specifier))
	{
		case 'd':
			o << std::dec;
			break;
		case 'e':
			o << std::scientific;
			break;
		case 'f':
			o << std::fixed;
			break;
		case 'o':
			o << std::oct;
			break;
		case 'x':
			o << std::hex;
			break;
	}
}

// formattable is a common virtual base class for formattable_object<T> objects,
// so that we can treat them as a homogeneous type to be put in a single std::array
// for indexing purposes.
struct formattable
{
	virtual ~formattable() {}
	virtual void format(std::ostream& o, size_t width, char specifier, size_t precision) const = 0;
};

// formattable_object<T> is the actual implementation.
template<class T>
struct formattable_object : public formattable
{
	formattable_object(const T& arg) : m_arg(arg) {};
	formattable_object(T&& arg) : m_arg(arg) {};

	virtual void format(std::ostream& o, size_t width, char specifier, size_t precision) const
	{
		auto_stream_state state_saver(o);
		print(m_arg, o, width, specifier, precision);
	}

	T m_arg;
};

// init_array recursively fills the array with formattable_object<T> containers
// for all the values in the parameter pack.
template<int A, int N, class First, class ... Rest>
void init_array_helper(std::array<std::unique_ptr<formattable>, A>& ary, First obj, Rest ... rest)
{
	ary[N].reset(static_cast<formattable*>(new formattable_object<First>(std::forward<First>(obj))));
	init_array_helper<A, N + 1, Rest...>(ary, std::forward<Rest>(rest)...);
}

template<int A, int N, class First>
void init_array_helper(std::array<std::unique_ptr<formattable>, A>& ary, First obj)
{
	ary[N].reset(static_cast<formattable*>(new formattable_object<First>(std::forward<First>(obj))));
}

template<int A, class ... Args>
void init_array(std::array<std::unique_ptr<formattable>, A>& ary, Args ... args)
{
	init_array_helper<A, 0, Args ...>(ary, std::forward<Args>(args)...);
}

template<class ... T>
struct formatter {
	formatter(const std::string& fmt) :
		m_fmt(fmt)
	{}

	formatter(const std::string& fmt, const T&... args) :
		m_fmt(fmt)
	{
		init_array(m_args, std::forward<T>(args)...);
	}

	formatter(const std::string& fmt, T&&... args) :
		m_fmt(fmt)
	{
		init_array(m_args, std::forward<T>(args)...);
	}

	// permit conversions to std::string for
	// std::string x = cppbits::format("x", 42);
	operator std::string()
	{
		std::stringstream ss;
		ss << *this;
		return std::move(ss.str());
	}

	std::string m_fmt;
	std::array<std::unique_ptr<formattable>, sizeof...(T)> m_args;
};

// Parse a format string and pull out argument index, field width, specifier, and
// precision parameters.
// TODO: This parser is really sloppy!
void parse_format_item_helper(
	std::string::const_iterator begin,
	std::string::const_iterator end,
	size_t& argument,
	size_t& width,
	char& specifier,
	size_t& precision)
{
	enum {
		kArgumentPosition,
		kWidth,
		kSpecifier,
		kPrecision
	} state = kArgumentPosition;

	for (auto itr = begin; itr != end; ++itr)
	{
		if (isdigit(*itr))
		{
			if (state == kArgumentPosition)
				argument = (argument * 10) + (*itr - '0');
			else if (state == kWidth)
				width = (width * 10) + (*itr - '0');
			else if (state == kPrecision)
				precision = (precision * 10) + (*itr - '0');
		}
		else if (isalpha(*itr))
		{
			if (state == kSpecifier)
			{
				specifier = *itr;
				state = kPrecision;
			}
		}
		else if (*itr == ':')
		{
			state = kSpecifier;
		}
		else if (*itr == ',')
		{
			state = kWidth;
		}
	}
}

template<class ... T>
void parse_format_item(
	std::ostream& o,
	const formatter<T...>& fmt,
	std::string::const_iterator begin, std::string::const_iterator end)
{
	size_t argument = 0;
	size_t width = 0;
	char specifier = 'G';
	size_t precision = 0;

	parse_format_item_helper(begin, end, argument, width, specifier, precision);

	if (argument < fmt.m_args.size())
	{
		fmt.m_args[argument]->format(o, width, specifier, precision);
	}
}

template<class ... T>
std::ostream& operator<<(std::ostream& o, const formatter<T...>& fmt) {
	auto itr = fmt.m_fmt.cbegin();

	while (itr != fmt.m_fmt.cend())
	{
		auto itr_to_brace = std::find(itr, fmt.m_fmt.cend(), '{');
		std::copy(itr, itr_to_brace, std::ostream_iterator<char>(o));
		if (itr_to_brace == fmt.m_fmt.cend())
			break;
		auto end_brace_itr = std::find(itr_to_brace, fmt.m_fmt.cend(), '}');
		if (end_brace_itr != fmt.m_fmt.cend())
		{
			parse_format_item(o, fmt, itr_to_brace, end_brace_itr);
			itr = end_brace_itr + 1;
		}
		else
		{
			itr = itr_to_brace;
		}
	}

	return o;
}

} // namespace detail

template<class ... T>
detail::formatter<T...> format(const std::string& str, T... args)
{
	return detail::formatter<T...>(str, std::forward<T>(args)...);
}

template<class T>
static void print(const T& arg, std::ostream& o, size_t width, char specifier, size_t precision)
{
	detail::default_format_handler(o, width, specifier, precision);
	o << arg;
}

};
